#include "goalc/compiler/Compiler.h"
#include "goalc/logger/Logger.h"

namespace {
bool get_inline_preference(Env* env) {
  auto ile = get_parent_env_of_type<WithInlineEnv>(env);
  if (ile) {
    return ile->inline_preference;
  } else {
    return false;
  }
}

const goos::Object& get_lambda_body(const goos::Object& def) {
  auto* iter = &def;
  while (true) {
    auto car = iter->as_pair()->car;
    if (car.is_symbol() && car.as_symbol()->name.at(0) == ':') {
      iter = &iter->as_pair()->cdr;
      iter = &iter->as_pair()->cdr;
    } else {
      assert(car.is_list());
      return iter->as_pair()->cdr;
    }
  }
}
}  // namespace

Val* Compiler::compile_inline(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {goos::ObjectType::SYMBOL}, {});

  auto kv = m_inlineable_functions.find(args.unnamed.at(0).as_symbol());
  if (kv == m_inlineable_functions.end()) {
    throw_compile_error(form, "Couldn't find function to inline!");
  }

  if (kv->second->func && !kv->second->func->settings.allow_inline) {
    throw_compile_error(form, "Found function to inline, but it isn't allowed.");
  }

  // todo, this should return a "view" of the lambda which indicates its inlined
  // so the correct label namespace behavior can be used.
  return kv->second;
}

Val* Compiler::compile_lambda(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto args = get_va(form, rest);
  if (args.unnamed.empty() || !args.unnamed.front().is_list() ||
      !args.only_contains_named({"name", "inline-only"})) {
    throw_compile_error(form, "Invalid lambda form");
  }

  auto place = fe->alloc_val<LambdaVal>(get_none()->type());
  auto& lambda = place->lambda;
  auto lambda_ts = m_ts.make_typespec("function");

  // parse the argument list.
  for_each_in_list(args.unnamed.front(), [&](const goos::Object& o) {
    if (o.is_symbol()) {
      // if it has no type, assume object.
      lambda.params.push_back({symbol_string(o), m_ts.make_typespec("object")});
      lambda_ts.add_arg(m_ts.make_typespec("object"));
    } else {
      auto param_args = get_va(o, o);
      va_check(o, param_args, {goos::ObjectType::SYMBOL, goos::ObjectType::SYMBOL}, {});

      GoalArg parm;
      parm.name = symbol_string(param_args.unnamed.at(0));
      parm.type = parse_typespec(param_args.unnamed.at(1));

      lambda.params.push_back(parm);
      lambda_ts.add_arg(parm.type);
    }
  });
  assert(lambda.params.size() == lambda_ts.arg_count());

  // optional name for debugging
  if (args.has_named("name")) {
    // todo, this probably prints a nasty error if name isn't a string.
    lambda.debug_name = symbol_string(args.get_named("name"));
  }

  lambda.body = get_lambda_body(rest);  // first is the argument list, rest is body
  place->func = nullptr;

  bool inline_only =
      args.has_named("inline-only") && symbol_string(args.get_named("inline-only")) != "#f";

  if (!inline_only) {
    // compile a function! First create env
    // auto new_func_env = fe->alloc_env<FunctionEnv>(env, lambda.debug_name);
    auto new_func_env = std::make_unique<FunctionEnv>(env, lambda.debug_name);
    new_func_env->set_segment(MAIN_SEGMENT);

    // set up arguments
    assert(lambda.params.size() < 8);  // todo graceful error
    for (u32 i = 0; i < lambda.params.size(); i++) {
      IRegConstraint constr;
      constr.instr_idx = 0;  // constraint at function start
      auto ireg = new_func_env->make_ireg(lambda.params.at(i).type, emitter::RegKind::GPR);
      constr.ireg = ireg->ireg();
      constr.desired_register = emitter::gRegInfo.get_arg_reg(i);
      new_func_env->params[lambda.params.at(i).name] = ireg;
      new_func_env->constrain(constr);
    }

    place->func = new_func_env.get();

    // nasty function block env setup
    auto return_reg = new_func_env->make_ireg(get_none()->type(), emitter::RegKind::GPR);
    auto func_block_env = new_func_env->alloc_env<BlockEnv>(new_func_env.get(), "#f");
    func_block_env->return_value = return_reg;
    func_block_env->end_label = Label(new_func_env.get());

    // compile the function!
    Val* result = nullptr;
    bool first_thing = true;
    for_each_in_list(lambda.body, [&](const goos::Object& o) {
      result = compile_error_guard(o, func_block_env);
      if (first_thing) {
        first_thing = false;
        // you could probably cheat and do a (begin (blorp) (declare ...)) to get around this.
        new_func_env->settings.is_set = true;
      }
    });
    if (result) {
      auto final_result = result->to_gpr(new_func_env.get());
      new_func_env->emit(std::make_unique<IR_Return>(return_reg, final_result));
      // new_func_env->emit(std::make_unique<IR_Null>())???
      new_func_env->finish();
      lambda_ts.add_arg(final_result->type());
    } else {
      lambda_ts.add_arg(m_ts.make_typespec("none"));
    }
    func_block_env->end_label.idx = new_func_env->code().size();

    auto obj_env = get_parent_env_of_type<FileEnv>(new_func_env.get());
    assert(obj_env);
    if (new_func_env->settings.save_code) {
      obj_env->add_function(std::move(new_func_env));
    }
  }

  place->set_type(lambda_ts);
  return place;
}

Val* Compiler::compile_function_or_method_call(const goos::Object& form, Env* env) {
  goos::Object f = form;
  auto fe = get_parent_env_of_type<FunctionEnv>(env);

  auto args = get_va(form, form);

  auto uneval_head = args.unnamed.at(0);
  Val* head = get_none();

  // determine if this call should be automatically inlined.
  // this logic will not trigger for a manually inlined call [using the (inline func) form]
  bool auto_inline = false;
  if (uneval_head.is_symbol()) {
    // we can only auto-inline the function if its name is explicit.
    // look it up:
    auto kv = m_inlineable_functions.find(uneval_head.as_symbol());
    if (kv != m_inlineable_functions.end()) {
      // it's inlinable.  However, we do not always inline an inlinable function by default
      if (kv->second->func ==
              nullptr ||  // only-inline, we must inline it as there is no code generated for it
          kv->second->func->settings
              .inline_by_default ||  // inline when possible, so we should inline
          (kv->second->func->settings.allow_inline &&
           get_inline_preference(env))) {  // inline is allowed, and we prefer it locally
        auto_inline = true;
        head = kv->second;
      }
    }
  }

  bool is_method_call = false;
  if (!auto_inline) {
    // if auto-inlining failed, we must get the thing to call in a different way.
    if (uneval_head.is_symbol()) {
      if (is_local_symbol(uneval_head, env) ||
          m_symbol_types.find(symbol_string(uneval_head)) != m_symbol_types.end()) {
        // the local environment (mlets, lexicals, constants, globals) defines this symbol.
        // this will "win" over a method name lookup, so we should compile as normal
        head = compile_error_guard(args.unnamed.front(), env);
      } else {
        // we don't think compiling the head give us a function, so it's either a method or an error
        is_method_call = true;
      }
    } else {
      // the head is some expression. Could be something like (inline my-func) or (-> obj
      // func-ptr-field) in either case, compile it - and it can't be a method call.
      head = compile_error_guard(args.unnamed.front(), env);
    }
  }

  if (!is_method_call) {
    // typecheck that we got a function
    typecheck(form, m_ts.make_typespec("function"), head->type(), "Function call head");
  }

  // compile arguments
  std::vector<RegVal*> eval_args;
  for (uint32_t i = 1; i < args.unnamed.size(); i++) {
    auto intermediate = compile_error_guard(args.unnamed.at(i), env);
    eval_args.push_back(intermediate->to_reg(env));
  }

  // see if its an "immediate" application. This happens in three cases:
  // 1). the user directly puts a (lambda ...) form in the head (like with a (let) macro)
  // 2). the user used a (inline my-func) to grab the LambdaPlace of the function.
  // 3). the auto-inlining above looked up the LambdaPlace of an inlinable_function.

  // note that an inlineable function looked up by symbol or other way WILL NOT cast to a
  // LambdaPlace! so this cast will only succeed if the auto-inliner succeeded, or the user has
  // passed use explicitly a lambda either with the lambda form, or with the (inline ...) form.
  LambdaVal* head_as_lambda = nullptr;
  if (!is_method_call) {
    head_as_lambda = dynamic_cast<LambdaVal*>(head);
  }

  if (head_as_lambda) {
    // inline the function!

    // check args are ok
    if (head_as_lambda->lambda.params.size() != eval_args.size()) {
      throw_compile_error(form, "invalid argument count");
    }

    // construct a lexical environment
    auto lexical_env = fe->alloc_env<LexicalEnv>(env);

    Env* compile_env = lexical_env;

    // if needed create a label env.
    // we don't want a separate label env with lets, but we do in other cases.
    if (auto_inline) {
      // TODO - this misses the case of (inline func)!
      compile_env = fe->alloc_env<LabelEnv>(lexical_env);
    }

    // check arg types
    if (!head->type().arg_count()) {
      if (head->type().arg_count() - 1 != eval_args.size()) {
        throw_compile_error(form, "invalid number of arguments to function call (inline)");
      }
      for (uint32_t i = 0; i < eval_args.size(); i++) {
        typecheck(form, head->type().get_arg(i), eval_args.at(i)->type(),
                  "function (inline) argument");
      }
    }

    // copy args...
    for (uint32_t i = 0; i < eval_args.size(); i++) {
      auto type = eval_args.at(i)->type();
      auto copy = env->make_ireg(type, get_preferred_reg_kind(type));
      env->emit(std::make_unique<IR_RegSet>(copy, eval_args.at(i)));
      lexical_env->vars[head_as_lambda->lambda.params.at(i).name] = copy;
    }

    // compile inline!
    bool first_thing = true;
    Val* result = get_none();
    for_each_in_list(head_as_lambda->lambda.body, [&](const goos::Object& o) {
      result = compile_error_guard(o, compile_env);
      if (first_thing) {
        first_thing = false;
        lexical_env->settings.is_set = true;
      }
    });

    // this doesn't require a return type.
    return result;
  } else {
    // not an inline call
    if (is_method_call) {
      // determine the method to call by looking at the type of first argument
      if (eval_args.empty()) {
        throw_compile_error(form, "0 argument method call is impossible to figure out");
      }
      assert(false);  // nyi
      // head = compile_get_method_of_object(eval_args.front(), symbol_string(uneval_head), env);
    }

    // convert the head to a GPR
    auto head_as_gpr =
        head->to_gpr(env);  // std::dynamic_pointer_cast<GprPlace>(resolve_to_gpr(head, env));
    if (head_as_gpr) {
      return compile_real_function_call(form, head_as_gpr, eval_args, env);
    } else {
      throw_compile_error(form, "can't figure out this function call!");
    }
  }

  throw_compile_error(form, "call_function_or_method unreachable");
  return get_none();
}

Val* Compiler::compile_real_function_call(const goos::Object& form,
                                          RegVal* function,
                                          const std::vector<RegVal*>& args,
                                          Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  fe->require_aligned_stack();
  TypeSpec return_ts;
  if (function->type().arg_count() == 0) {
    // if the type system doesn't know what the function will return, just make it object.
    // the user is responsible for getting this right.
    return_ts = m_ts.make_typespec("object");
    gLogger.log(MSG_WARN, "[Warning] Function call could not determine return type: %s\n",
                form.print().c_str());
    // todo, should this be a warning?  not a great thing if we don't know what a function will
    // return?
  } else {
    return_ts = function->type().last_arg();
  }

  auto return_reg = env->make_ireg(return_ts, emitter::RegKind::GPR);

  // TODO - VERY IMPORTANT
  // CREATE A TEMP COPY OF FUNCTION! WILL BE DESTROYED.

  // nope! not anymore.
  //  for(auto& arg : args) {
  //    // note: this has to be done in here, because we might want to const prop across lexical
  //    envs. arg = resolve_to_gpr(arg, env);
  //  }

  // check arg count:
  if (function->type().arg_count()) {
    if (function->type().arg_count() - 1 != args.size()) {
      printf("got type %s\n", function->type().print().c_str());
      throw_compile_error(form, "invalid number of arguments to function call: got " +
                                    std::to_string(args.size()) + " and expected " +
                                    std::to_string(function->type().arg_count() - 1));
    }
    for (uint32_t i = 0; i < args.size(); i++) {
      typecheck(form, function->type().get_arg(i), args.at(i)->type(), "function argument");
    }
  }

  // set args (introducing a move here makes coloring more likely to be possible)
  std::vector<RegVal*> arg_outs;
  for (auto& arg : args) {
    arg_outs.push_back(env->make_ireg(arg->type(), emitter::RegKind::GPR));
    env->emit(std::make_unique<IR_RegSet>(arg_outs.back(), arg));
  }

  env->emit(std::make_unique<IR_FunctionCall>(function, return_reg, arg_outs));

  if (m_settings.emit_move_after_return) {
    auto result_reg = env->make_gpr(return_reg->type());
    env->emit(std::make_unique<IR_RegSet>(result_reg, return_reg));
    return result_reg;
  } else {
    return return_reg;
  }
}