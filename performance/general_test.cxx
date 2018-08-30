#include <iostream>

#include "dynamic_index/singlethread/persist_trie/persist_trie.h"
#include "generic_key.h"
#include "fast_random.h"

void test() {

  PersistTrie trie;

  size_t n = 10;
  FastRandom rand_gen;
  size_t key_size = 8;
  GenericKey key(key_size);

  std::vector<GenericKey> keys;

  for (size_t i = 0 ; i < n; ++i) {
    rand_gen.next_readable_chars(key_size, key.raw());
    uint64_t value = i + 2048;

    keys.push_back(key);
    trie.insert((unsigned char*)key.raw(), key_size, value);
  }

  std::vector<uint64_t> ret_vals;
  for (auto &entry : keys) {
    trie.find((unsigned char*)entry.raw(), key_size, ret_vals);
    if (ret_vals.size() != 0) {
      std::cout << ret_vals.at(0) << std::endl;
    } else {
      std::cout << "found nothing!" << std::endl;
    }
    ret_vals.clear();
  }

}

int main() {
  test();

}