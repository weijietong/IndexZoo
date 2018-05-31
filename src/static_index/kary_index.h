#pragma once

#include <vector>
#include <cmath>

#include "base_static_index.h"


namespace static_index {

template<typename KeyT, typename ValueT>
class KAryIndex : public BaseStaticIndex<KeyT, ValueT> {

public:
  KAryIndex(DataTable<KeyT, ValueT> *table_ptr, const size_t num_layers, const size_t k) : BaseStaticIndex<KeyT, ValueT>(table_ptr), num_layers_(num_layers), k_(k) {
    ASSERT(k_ >= 2, "k must be larger than or equal to 2");
  }

  virtual ~KAryIndex() {
    if (num_layers_ != 0) {
      delete[] inner_nodes_;
      inner_nodes_ = nullptr;
    }
  }

  virtual void find(const KeyT &key, std::vector<Uint64> &values) final {

    if (this->size_ == 0) {
      return;
    }

    if (key > key_max_ || key < key_min_) {
      return;
    }
    if (key_max_ == key_min_) {
      if (key_max_ == key) {
        for (size_t i = 0; i < this->size_; ++i) {
          values.push_back(this->container_[i].value_);
        }
      }
      return;
    }

    size_t offset_find = this->size_;
    std::pair<int, int> offset_range = find_inner_layers(key);
    if (offset_range.first == offset_range.second) {
      offset_find = offset_range.first;
    } else {
      offset_find = find_internal(key, offset_range.first, offset_range.second);
    }

    if (offset_find == this->size_) {
      // find nothing
      return;
    }

    values.push_back(this->container_[offset_find].value_);

    // move left
    int offset_find_lhs = offset_find - 1;
    while (offset_find_lhs >= 0) {

      if (this->container_[offset_find_lhs].key_ == key) {
        values.push_back(this->container_[offset_find_lhs].value_);
        offset_find_lhs -= 1;
      } else {
        break;
      }
    }
    // move right
    int offset_find_rhs = offset_find + 1;
    while (offset_find_rhs <= this->size_ - 1) {

      if (this->container_[offset_find_rhs].key_ == key) {
        values.push_back(this->container_[offset_find_rhs].value_);
        offset_find_rhs += 1;
      } else {
        break;
      }
    }
  }


  virtual void find_range(const KeyT &lhs_key, const KeyT &rhs_key, std::vector<Uint64> &values) final {
    assert(lhs_key < rhs_key);

    if (this->size_ == 0) {
      return;
    }
    if (lhs_key > key_max_ || rhs_key < key_min_) {
      return;
    }

  }

  virtual void reorganize() final {

    this->base_reorganize();

    size_t inner_node_size = std::pow(k_, num_layers_) - 1;

    ASSERT(inner_node_size < this->size_, "exceed maximum layers");

    key_min_ = this->container_[0].key_;
    key_max_ = this->container_[this->size_ - 1].key_;

    if (num_layers_ != 0) {
      size_t inner_size = std::pow(k_, num_layers_) - 1;
      inner_nodes_ = new KeyT[inner_size];
      construct_inner_layers();
    } else {
      inner_nodes_ = nullptr;
    }
  }

  virtual void print() const final {
    if (inner_nodes_ != nullptr) {
      size_t inner_size = std::pow(k_, num_layers_) - 1;
      for (size_t i = 0; i < inner_size; ++i) {
        std::cout << inner_nodes_[i] << " ";
      }
      std::cout << std::endl;
    }
  }

private:

  void construct_inner_layers() {
    ASSERT (num_layers_ != 0, "number of layers cannot be 0");

    size_t begin_offset = 0;
    size_t end_offset = this->size_ - 1;

    size_t step_offset = (end_offset - begin_offset) / k_;
    
    for (size_t i = 0; i < k_ - 1; ++i) {
      inner_nodes_[i] = this->container_[begin_offset + step_offset * (i + 1)].key_;
    }
    if (num_layers_ == 1) { return; }

    size_t base_pos = k_ - 1;
    size_t next_layer = 1;
    construct_inner_layers_internal(begin_offset, begin_offset + step_offset - 1, base_pos, 0, next_layer);
    for (size_t i = 1; i < k_ - 1; ++i) {
      construct_inner_layers_internal(begin_offset + step_offset * i + 1, begin_offset + step_offset * (i + 1) - 1, base_pos, i * (k_ - 1), next_layer);
    }
    construct_inner_layers_internal(begin_offset + step_offset * (k_ - 1) + 1, end_offset, base_pos, (k_ - 1) * (k_ - 1), next_layer);
  }

  void construct_inner_layers_internal(const int begin_offset, const int end_offset, const size_t base_pos, const size_t dst_pos, const size_t curr_layer) {
    if (begin_offset > end_offset) { return; }

    size_t step_offset = (end_offset - begin_offset) / k_;
    
    for (size_t i = 0; i < k_ - 1; ++i) {
      inner_nodes_[base_pos + dst_pos + i] = this->container_[begin_offset + step_offset * (i + 1)].key_;
    }
    if (num_layers_ == curr_layer + 1) { return; }

    size_t new_base_pos = (base_pos + 1) * k_ - 1;
    size_t new_dst_pos = dst_pos * k_;
    size_t next_layer = curr_layer + 1;
    construct_inner_layers_internal(begin_offset, begin_offset + step_offset - 1, new_base_pos, new_dst_pos, next_layer);
    for (size_t i = 1; i < k_ - 1; ++i) {
      construct_inner_layers_internal(begin_offset + step_offset * i + 1, begin_offset + step_offset * (i + 1) - 1, new_base_pos, new_dst_pos + i * (k_ - 1), next_layer);
    }
    construct_inner_layers_internal(begin_offset + step_offset * (k_ - 1) + 1, end_offset, new_base_pos, new_dst_pos + (k_ - 1) * (k_ - 1), next_layer);
  }

  // binary search
  // this function directly find key in leaf nodes
  size_t find_internal(const KeyT &key, const int offset_begin, const int offset_end) {
    if (offset_begin > offset_end) {
      return this->size_;
    }
    int offset_lookup = (offset_begin + offset_end) / 2;
    KeyT key_lookup = this->container_[offset_lookup].key_;
    if (key == key_lookup) {
      return offset_lookup;
    }
    if (key > key_lookup) {
      return find_internal(key, offset_lookup + 1, offset_end);
    } else {
      return find_internal(key, offset_begin, offset_lookup - 1);
    }
  }

  // find key in inner nodes
  std::pair<int, int> find_inner_layers(const KeyT &key) {

    if (num_layers_ == 0) { return std::pair<int, int>(0, this->size_); }

    int begin_offset = 0;
    int end_offset = this->size_ - 1;

    size_t step_offset = (end_offset - begin_offset) / k_;

    for (size_t i = 0; i < k_ - 1; ++i) {
      if (key == inner_nodes_[i]) { return std::pair<int, int>(begin_offset + step_offset * (i + 1), begin_offset + step_offset * (i + 1)); }
    }

    size_t base_pos = k_ - 1;
    size_t next_layer = 1;

    if (key < inner_nodes_[0]) {
      return find_inner_layers_internal(key, begin_offset, begin_offset + step_offset - 1, base_pos, 0, next_layer);
    }

    for (size_t i = 1; i < k_ - 1; ++i) {
      if (key < inner_nodes_[i]) {
        return find_inner_layers_internal(key, begin_offset + step_offset * i + 1, begin_offset + step_offset * (i + 1) - 1, base_pos, i * (k_ - 1), next_layer);
      }
    }

    return find_inner_layers_internal(key, begin_offset + step_offset * (k_ - 1) + 1, end_offset, base_pos, (k_ - 1) * (k_ - 1), next_layer);
  }

  // helper function for find_inner_layers()
  std::pair<int, int> find_inner_layers_internal(const KeyT &key, const int begin_offset, const int end_offset, const size_t base_pos, const size_t dst_pos, const size_t curr_layer) {

    if (num_layers_ == curr_layer) { return std::pair<int, int>(begin_offset, end_offset); }

    size_t step_offset = (end_offset - begin_offset) / k_;

    for (size_t i = 0; i < k_ - 1; ++i) {
      if (key == inner_nodes_[base_pos + dst_pos + i]) { 
        return std::pair<int, int>(begin_offset + step_offset * (i + 1), begin_offset + step_offset * (i + 1)); }
    }

    size_t new_base_pos = (base_pos + 1) * k_ - 1;
    size_t new_dst_pos = dst_pos * k_;
    size_t next_layer = curr_layer + 1;

    if (key < inner_nodes_[base_pos + dst_pos]) {
      return find_inner_layers_internal(key, begin_offset, begin_offset + step_offset - 1, new_base_pos, new_dst_pos, next_layer);
    }

    for (size_t i = 1; i < k_ - 1; ++i) {
      if (key < inner_nodes_[base_pos + dst_pos + i]) {
        return find_inner_layers_internal(key, begin_offset + step_offset * i + 1, begin_offset + step_offset * (i + 1) - 1, new_base_pos, new_dst_pos + i * (k_ - 1), next_layer);
      }
    }

    return find_inner_layers_internal(key, begin_offset + step_offset * (k_ - 1) + 1, end_offset, new_base_pos, new_dst_pos + (k_ - 1) * (k_ - 1), next_layer);
  }


private:

  size_t num_layers_;
  size_t k_;

  KeyT key_min_;
  KeyT key_max_;
  KeyT *inner_nodes_;

};

}



