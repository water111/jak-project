#ifndef JAK_DGOWRITER_H
#define JAK_DGOWRITER_H

#include <vector>

struct DgoDescription {
  std::string dgo_name;
  struct DgoEntry {
    std::string file_name;
    std::string name_in_dgo;
  };
  std::vector<DgoEntry> entries;
};

void build_dgo(const DgoDescription& description);

#endif  // JAK_DGOWRITER_H
