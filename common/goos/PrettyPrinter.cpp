#include <cassert>
#include <stdexcept>
#include <utility>
#include "PrettyPrinter.h"
#include "Reader.h"

namespace pretty_print {

/*!
 * A single token which cannot be split between lines.
 */
struct FormToken {
  enum class TokenKind {
    WHITESPACE,
    STRING,
    OPEN_PAREN,
    DOT,
    CLOSE_PAREN,
    EMPTY_PAIR,
    SPECIAL_STRING  // has different alignment rules than STRING
  } kind;
  explicit FormToken(TokenKind _kind, std::string _str = "") : kind(_kind), str(std::move(_str)) {}

  std::string str;

  std::string toString() const {
    std::string s;
    switch (kind) {
      case TokenKind::WHITESPACE:
        s.push_back(' ');
        break;
      case TokenKind::STRING:
        s.append(str);
        break;
      case TokenKind::OPEN_PAREN:
        s.push_back('(');
        break;
      case TokenKind::DOT:
        s.push_back('.');
        break;
      case TokenKind::CLOSE_PAREN:
        s.push_back(')');
        break;
      case TokenKind::EMPTY_PAIR:
        s.append("()");
        break;
      case TokenKind::SPECIAL_STRING:
        s.append(str);
        break;
      default:
        throw std::runtime_error("toString unknown token kind");
    }
    return s;
  }
};

/*!
 * Convert a GOOS object to tokens and add it to the list.
 * This is the main function which recursively builds a list of tokens out of an s-expression.
 *
 * Note that not all GOOS objects can be pretty printed. Only the ones that can be directly
 * generated by the reader.
 */
void add_to_token_list(const goos::Object& obj, std::vector<FormToken>* tokens) {
  switch (obj.type) {
    case goos::ObjectType::EMPTY_LIST:
      tokens->emplace_back(FormToken::TokenKind::EMPTY_PAIR);
      break;
      // all of these can just be printed to a string and turned into a 'symbol'
    case goos::ObjectType::INTEGER:
    case goos::ObjectType::FLOAT:
    case goos::ObjectType::CHAR:
    case goos::ObjectType::SYMBOL:
    case goos::ObjectType::STRING:
      tokens->emplace_back(FormToken::TokenKind::STRING, obj.print());
      break;

      // it's important to break the pair up into smaller tokens which can then be split
      // across lines.
    case goos::ObjectType::PAIR: {
      tokens->emplace_back(FormToken::TokenKind::OPEN_PAREN);
      auto* to_print = &obj;
      for (;;) {
        if (to_print->is_pair()) {
          // first print the car into our token list:
          add_to_token_list(to_print->as_pair()->car, tokens);
          // then load up the cdr as the next thing to print
          to_print = &to_print->as_pair()->cdr;
          if (to_print->is_empty_list()) {
            // we're done, add a close paren and finish
            tokens->emplace_back(FormToken::TokenKind::CLOSE_PAREN);
            return;
          } else {
            // more to print, add whitespace
            tokens->emplace_back(FormToken::TokenKind::WHITESPACE);
          }
        } else {
          // got an improper list.
          // add a dot, space
          tokens->emplace_back(FormToken::TokenKind::DOT);
          tokens->emplace_back(FormToken::TokenKind::WHITESPACE);
          // then the thing and a close paren.
          add_to_token_list(*to_print, tokens);
          tokens->emplace_back(FormToken::TokenKind::CLOSE_PAREN);
          return;  // and we're done with this list.
        }
      }
    } break;

      // these are unsupported by the pretty printer.
    case goos::ObjectType::ARRAY:  // todo, we should probably handle arrays.
    case goos::ObjectType::LAMBDA:
    case goos::ObjectType::MACRO:
    case goos::ObjectType::ENVIRONMENT:
      throw std::runtime_error("tried to pretty print a goos object kind which is not supported.");
    default:
      assert(false);
  }
}

/*!
 * Linked list node representing a token in the output (whitespace, paren, newline, etc)
 */
struct PrettyPrinterNode {
  FormToken* tok = nullptr;  // if we aren't a newline, we will have a token.
  int line = -1;             // line that token occurs on. undef for newlines
  int lineIndent = -1;       // indent of line.  only valid for first token in the line
  int offset = -1;           // offset of beginning of token from left margin
  int specialIndentDelta = 0;
  bool is_line_separator = false;                      // true if line separator (not a token)
  PrettyPrinterNode *next = nullptr, *prev = nullptr;  // linked list
  PrettyPrinterNode* paren =
      nullptr;  // pointer to open paren if in parens.  open paren points to close and vice versa
  explicit PrettyPrinterNode(FormToken* _tok) { tok = _tok; }
  PrettyPrinterNode() = default;
};

/*!
 * Container to track and cleanup all nodes after use.
 */
struct NodePool {
  std::vector<PrettyPrinterNode*> nodes;
  PrettyPrinterNode* allocate(FormToken* x) {
    auto result = new PrettyPrinterNode(x);
    nodes.push_back(result);
    return result;
  }

  PrettyPrinterNode* allocate() {
    auto result = new PrettyPrinterNode;
    nodes.push_back(result);
    return result;
  }

  NodePool() = default;

  ~NodePool() {
    for (auto& x : nodes) {
      delete x;
    }
  }

  // so we don't accidentally copy this.
  NodePool& operator=(const NodePool&) = delete;
  NodePool(const NodePool&) = delete;
};

/*!
 * Splice in a line break after the given node, it there isn't one already and if it isn't the last
 * node.
 */
void insertNewlineAfter(NodePool& pool, PrettyPrinterNode* node, int specialIndentDelta) {
  if (node->next && !node->next->is_line_separator) {
    auto* nl = pool.allocate();
    auto* next = node->next;
    node->next = nl;
    nl->prev = node;
    nl->next = next;
    next->prev = nl;
    nl->is_line_separator = true;
    nl->specialIndentDelta = specialIndentDelta;
  }
}

/*!
 * Splice in a line break before the given node, if there isn't one already and if it isn't the
 * first node.
 */
void insertNewlineBefore(NodePool& pool, PrettyPrinterNode* node, int specialIndentDelta) {
  if (node->prev && !node->prev->is_line_separator) {
    auto* nl = pool.allocate();
    auto* prev = node->prev;
    prev->next = nl;
    nl->prev = prev;
    nl->next = node;
    node->prev = nl;
    nl->is_line_separator = true;
    nl->specialIndentDelta = specialIndentDelta;
  }
}

/*!
 * Break a list across multiple lines. This is how line lengths are decreased.
 * This does not compute the proper indentation and leaves the list in a bad state.
 * After this has been called, the entire selection should be reformatted with propagate_pretty
 */
void breakList(NodePool& pool, PrettyPrinterNode* leftParen) {
  assert(!leftParen->is_line_separator);
  assert(leftParen->tok->kind == FormToken::TokenKind::OPEN_PAREN);
  auto* rp = leftParen->paren;
  assert(rp->tok->kind == FormToken::TokenKind::CLOSE_PAREN);

  for (auto* n = leftParen->next; n && n != rp; n = n->next) {
    if (!n->is_line_separator) {
      if (n->tok->kind == FormToken::TokenKind::OPEN_PAREN) {
        n = n->paren;
        assert(n->tok->kind == FormToken::TokenKind::CLOSE_PAREN);
        insertNewlineAfter(pool, n, 0);
      } else if (n->tok->kind != FormToken::TokenKind::WHITESPACE) {
        assert(n->tok->kind != FormToken::TokenKind::CLOSE_PAREN);
        insertNewlineAfter(pool, n, 0);
      }
    }
  }
}

/*!
 * Compute proper line numbers, offsets, and indents for a list of tokens with newlines
 * Will add newlines for close parens if needed.
 */
static PrettyPrinterNode* propagatePretty(NodePool& pool,
                                          PrettyPrinterNode* list,
                                          int line_length) {
  // propagate line numbers
  PrettyPrinterNode* rv = nullptr;
  int line = list->line;
  for (auto* n = list; n; n = n->next) {
    if (n->is_line_separator) {
      line++;
    } else {
      n->line = line;
      // add the weird newline.
      if (n->tok->kind == FormToken::TokenKind::CLOSE_PAREN) {
        if (n->line != n->paren->line) {
          if (n->prev && !n->prev->is_line_separator) {
            insertNewlineBefore(pool, n, 0);
            line++;
          }
          if (n->next && !n->next->is_line_separator) {
            insertNewlineAfter(pool, n, 0);
          }
        }
      }
    }
  }

  // compute offsets and indents
  std::vector<int> indentStack;
  indentStack.push_back(0);
  int offset = 0;
  PrettyPrinterNode* line_start = list;
  bool previous_line_sep = false;
  for (auto* n = list; n; n = n->next) {
    if (n->is_line_separator) {
      previous_line_sep = true;
      offset = indentStack.back() += n->specialIndentDelta;
    } else {
      if (previous_line_sep) {
        line_start = n;
        n->lineIndent = offset;
        previous_line_sep = false;
      }

      n->offset = offset;
      offset += n->tok->toString().length();
      if (offset > line_length && !rv)
        rv = line_start;
      if (n->tok->kind == FormToken::TokenKind::OPEN_PAREN) {
        if (!n->prev || n->prev->is_line_separator) {
          indentStack.push_back(offset + 1);
        } else {
          indentStack.push_back(offset - 1);
        }
      }

      if (n->tok->kind == FormToken::TokenKind::CLOSE_PAREN) {
        indentStack.pop_back();
      }
    }
  }
  return rv;
}

/*!
 * Get the token on the start of the next line. nullptr if we're the last line.
 */
PrettyPrinterNode* getNextLine(PrettyPrinterNode* start) {
  assert(!start->is_line_separator);
  int line = start->line;
  for (;;) {
    if (start->is_line_separator || start->line == line) {
      if (start->next)
        start = start->next;
      else
        return nullptr;
    } else {
      break;
    }
  }
  return start;
}

/*!
 * Get the next open paren on the current line (can start in the middle of line, not inclusive of
 * start) nullptr if there's no open parens on the rest of this line.
 */
PrettyPrinterNode* getNextListOnLine(PrettyPrinterNode* start) {
  int line = start->line;
  assert(!start->is_line_separator);
  if (!start->next || start->next->is_line_separator)
    return nullptr;
  start = start->next;
  while (!start->is_line_separator && start->line == line) {
    if (start->tok->kind == FormToken::TokenKind::OPEN_PAREN)
      return start;
    if (!start->next)
      return nullptr;
    start = start->next;
  }
  return nullptr;
}

/*!
 * Get the first open paren on the current line (can start in the middle of line, inclusive of
 * start) nullptr if there's no open parens on the rest of this line
 */
PrettyPrinterNode* getFirstListOnLine(PrettyPrinterNode* start) {
  int line = start->line;
  assert(!start->is_line_separator);
  while (!start->is_line_separator && start->line == line) {
    if (start->tok->kind == FormToken::TokenKind::OPEN_PAREN)
      return start;
    if (!start->next)
      return nullptr;
    start = start->next;
  }
  return nullptr;
}

/*!
 * Get the first token on the first line which exceeds the max length
 */
PrettyPrinterNode* getFirstBadLine(PrettyPrinterNode* start, int line_length) {
  assert(!start->is_line_separator);
  int currentLine = start->line;
  auto* currentLineNode = start;
  for (;;) {
    if (start->is_line_separator) {
      assert(start->next);
      start = start->next;
    } else {
      if (start->line != currentLine) {
        currentLine = start->line;
        currentLineNode = start;
      }
      if (start->offset > line_length) {
        return currentLineNode;
      }
      if (!start->next) {
        return nullptr;
      }
      start = start->next;
    }
  }
}

/*!
 * Break insertion algorithm.
 */
void insertBreaksAsNeeded(NodePool& pool, PrettyPrinterNode* head, int line_length) {
  PrettyPrinterNode* last_line_complete = nullptr;
  PrettyPrinterNode* line_to_start_line_search = head;

  // loop over lines
  for (;;) {
    // compute lines as needed
    propagatePretty(pool, head, line_length);

    // search for a bad line starting at the last line we fixed
    PrettyPrinterNode* candidate_line = getFirstBadLine(line_to_start_line_search, line_length);
    // if we got the same line we started on, this means we couldn't fix it.
    if (candidate_line == last_line_complete) {
      candidate_line = nullptr;  // so we say our candidate was bad and try to find another
      PrettyPrinterNode* next_line = getNextLine(line_to_start_line_search);
      if (next_line) {
        candidate_line = getFirstBadLine(next_line, line_length);
      }
    }
    if (!candidate_line)
      break;

    // okay, we have a line which needs fixing.
    assert(!candidate_line->prev || candidate_line->prev->is_line_separator);
    PrettyPrinterNode* form_to_start = getFirstListOnLine(candidate_line);
    for (;;) {
      if (!form_to_start) {
        // this means we failed to hit the desired line length...
        break;
      }
      breakList(pool, form_to_start);
      propagatePretty(pool, head, line_length);
      if (getFirstBadLine(candidate_line, line_length) != candidate_line) {
        break;
      }

      form_to_start = getNextListOnLine(form_to_start);
      if (!form_to_start)
        break;
    }

    last_line_complete = candidate_line;
    line_to_start_line_search = candidate_line;
  }
}

void insertSpecialBreaks(NodePool& pool, PrettyPrinterNode* node) {
  for (; node; node = node->next) {
    if (!node->is_line_separator && node->tok->kind == FormToken::TokenKind::STRING) {
      std::string& name = node->tok->str;
      if (name == "deftype") {  // todo!
        auto* parent_type_dec = getNextListOnLine(node);
        if (parent_type_dec) {
          insertNewlineAfter(pool, parent_type_dec->paren, 0);
        }
      }

      if (name == "defun" || name == "defmethod") {
        auto* parent_type_dec = getNextListOnLine(node);
        if (parent_type_dec) {
          insertNewlineAfter(pool, parent_type_dec->paren, 0);
        }
      }

      if (name.at(0) == '"') {
        insertNewlineAfter(pool, node, 0);
      }
    }
  }
}

std::string to_string(const goos::Object& obj, int line_length) {
  NodePool pool;
  std::vector<FormToken> tokens;
  add_to_token_list(obj, &tokens);
  assert(!tokens.empty());
  std::string pretty;

  // build linked list of nodes
  PrettyPrinterNode* head = pool.allocate(&tokens[0]);
  PrettyPrinterNode* node = head;
  head->line = 0;
  head->offset = 0;
  head->lineIndent = 0;
  int offset = head->tok->toString().length();
  for (size_t i = 1; i < tokens.size(); i++) {
    node->next = pool.allocate(&tokens[i]);
    node->next->prev = node;
    node = node->next;
    node->line = 0;
    node->offset = offset;
    offset += node->tok->toString().length();
    node->lineIndent = 0;
  }

  // attach parens.
  std::vector<PrettyPrinterNode*> parenStack;
  parenStack.push_back(nullptr);
  for (PrettyPrinterNode* n = head; n; n = n->next) {
    if (n->tok->kind == FormToken::TokenKind::OPEN_PAREN) {
      parenStack.push_back(n);
    } else if (n->tok->kind == FormToken::TokenKind::CLOSE_PAREN) {
      n->paren = parenStack.back();
      parenStack.back()->paren = n;
      parenStack.pop_back();
    } else {
      n->paren = parenStack.back();
    }
  }
  assert(parenStack.size() == 1);
  assert(!parenStack.back());

  insertSpecialBreaks(pool, head);
  propagatePretty(pool, head, line_length);
  insertBreaksAsNeeded(pool, head, line_length);

  // write to string
  bool newline_prev = true;
  for (PrettyPrinterNode* n = head; n; n = n->next) {
    if (n->is_line_separator) {
      pretty.push_back('\n');
      newline_prev = true;
    } else {
      if (newline_prev) {
        pretty.append(n->lineIndent, ' ');
        newline_prev = false;
        if (n->tok->kind == FormToken::TokenKind::WHITESPACE)
          continue;
      }
      pretty.append(n->tok->toString());
    }
  }

  return pretty;
}

goos::Reader pretty_printer_reader;

goos::Reader& get_pretty_printer_reader() {
  return pretty_printer_reader;
}

goos::Object to_symbol(const std::string& str) {
  return goos::SymbolObject::make_new(pretty_printer_reader.symbolTable, str);
}

goos::Object build_list(const std::string& str) {
  return build_list(to_symbol(str));
}

goos::Object build_list(const goos::Object& obj) {
  return goos::PairObject::make_new(obj, goos::EmptyListObject::make_new());
}

goos::Object build_list(const std::vector<goos::Object>& objects) {
  if (objects.empty()) {
    return goos::EmptyListObject::make_new();
  } else {
    return build_list(objects.data(), objects.size());
  }
}

// build a list out of an array of forms
goos::Object build_list(const goos::Object* objects, int count) {
  assert(count);
  auto car = objects[0];
  goos::Object cdr;
  if (count - 1) {
    cdr = build_list(objects + 1, count - 1);
  } else {
    cdr = goos::EmptyListObject::make_new();
  }
  return goos::PairObject::make_new(car, cdr);
}

// build a list out of a vector of strings that are converted to symbols
goos::Object build_list(const std::vector<std::string>& symbols) {
  if (symbols.empty()) {
    return goos::EmptyListObject::make_new();
  }
  std::vector<goos::Object> f;
  f.reserve(symbols.size());
  for (auto& x : symbols) {
    f.push_back(to_symbol(x));
  }
  return build_list(f.data(), f.size());
}
}  // namespace pretty_print