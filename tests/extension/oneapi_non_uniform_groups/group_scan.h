/*******************************************************************************
//
//  SYCL 2020 Conformance Test Suite
//
//  Copyright (c) 2024 The Khronos Group Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
*******************************************************************************/

#include <map>
#include <valarray>

#include "../../group_functions/group_functions_common.h"
#include "non_uniform_group_common.h"

template <typename GroupT, typename T, typename U, typename I, typename OpT>
class joint_scan_group_kernel;

// This should never be higher than std::numeric_limits<T>::max() for the
// smallest type tested. Currently, the smallest type tested is
// char/int8_t, so it shouldn't be higher than 127.
constexpr int init = 42;

// Test size must be a multiple of all the used fixed_size_group sizes.
constexpr size_t test_size = 8;

template <typename I, typename T, typename U, typename Group, typename OpT>
auto joint_inclusive_scan_helper(Group group, T* v_begin, T* v_end,
                                 U* r_i_begin, OpT op, bool with_init) {
  if (with_init) {
    return sycl::joint_inclusive_scan(group, v_begin, v_end, r_i_begin, op,
                                      I(init));
  }
  assert((std::is_same_v<I, U> &&
          "Without init value I and U should be the same type."));
  return (U*)sycl::joint_inclusive_scan(group, v_begin, v_end, (I*)r_i_begin,
                                        op);
}

template <typename I, typename T, typename U, typename Group, typename OpT>
auto joint_exclusive_scan_helper(Group group, T* v_begin, T* v_end,
                                 U* r_e_begin, OpT op, bool with_init) {
  if (with_init) {
    return sycl::joint_exclusive_scan(group, v_begin, v_end, r_e_begin, I(init),
                                      op);
  }
  assert((std::is_same_v<I, U> &&
          "Without init value I and U should be the same type."));
  return (U*)sycl::joint_exclusive_scan(group, v_begin, v_end, (I*)r_e_begin,
                                        op);
}

template <typename T, typename U, typename I, typename OpT>
struct JointScanDataStruct {
  JointScanDataStruct(size_t range_size, OpT op, bool with_init)
      : ref_input(range_size), res(range_size * 2, U(-1)) {
    std::iota(ref_input.begin(), ref_input.end(), T(1));
    if constexpr (std::is_same_v<OpT, sycl::multiplies<I>> ||
                  std::is_same_v<OpT, sycl::plus<I>>) {
      auto identity = sycl::known_identity_v<OpT, I>;
      auto acc = with_init ? I{init} : identity;
      for (size_t i = 0; i < range_size; ++i) {
        I tmp = op(I(acc), I(ref_input[i]));
        if (tmp > std::numeric_limits<U>::max()) {
          ref_input[i] = identity;
        }
        acc = op(acc, ref_input[i]);
      }
    }
  }

  void check_results(size_t range_size, OpT op, const std::string& op_name,
                     const std::string& group_name, bool with_init) {
    CHECK(end[0]);
    CHECK(end[1]);
    CHECK(ret_type[0]);
    CHECK(ret_type[1]);

    I init_value = with_init ? I(init) : sycl::known_identity<OpT, I>::value;

    std::vector<U> reference_e(range_size, U(-1));
    std::vector<U> reference_i(range_size, U(-1));
    std::exclusive_scan(ref_input.begin(), ref_input.end(), reference_e.begin(),
                        init_value, op);
    std::inclusive_scan(ref_input.begin(), ref_input.end(), reference_i.begin(),
                        op, init_value);
    for (int i = 0; i < range_size; i++) {
      {
        INFO("Check joint_exclusive_scan on " + group_name + " for element " +
             std::to_string(i) + " (Operator: " + op_name + ")");
        INFO("Result: " + std::to_string(res[i]));
        INFO("Expected: " + std::to_string(reference_e[i]));
        CHECK(res[i] == reference_e[i]);
      }
      {
        INFO("Check joint_inclusive_scan on " + group_name + " for element " +
             std::to_string(i) + " (Operator: " + op_name + ")");
        INFO("Result: " + std::to_string(res[i + range_size]));
        INFO("Expected: " + std::to_string(reference_i[i]));
        CHECK(res[i + range_size] == reference_i[i]);
      }
    }
  }

  sycl::buffer<T, 1> create_ref_input_buffer() {
    return {ref_input.data(), ref_input.size()};
  }

  sycl::buffer<U, 1> create_res_buffer() { return {res.data(), res.size()}; }

  sycl::buffer<bool, 1> create_end_buffer() { return {end, 2}; }

  sycl::buffer<bool, 1> create_ret_type_buffer() { return {ret_type, 2}; }

  std::vector<T> ref_input;
  std::vector<U> res;
  bool end[2] = {false, false};
  bool ret_type[2] = {false, false};
  std::vector<size_t> local_id;
};

template <typename GroupT, typename T, typename U, typename I = U, typename OpT>
void check_scan(sycl::queue& queue, size_t size,
                sycl::nd_range<1> executionRange, OpT op,
                const std::string& op_name, bool with_init) {
  const std::string group_name = NonUniformGroupHelper<GroupT>::get_name();

  for (size_t test_case = 0;
       test_case < NonUniformGroupHelper<GroupT>::num_test_cases; ++test_case) {
    const std::string test_case_name =
        NonUniformGroupHelper<GroupT>::get_test_case_name(test_case);
    INFO("Running test case (" + std::to_string(test_case) + ") with " +
         test_case_name);

    JointScanDataStruct<T, U, I, OpT> host_data{size, op, with_init};
    {
      sycl::buffer<T, 1> ref_input_sycl = host_data.create_ref_input_buffer();
      sycl::buffer<U, 1> res_sycl = host_data.create_res_buffer();
      sycl::buffer<bool, 1> end_sycl = host_data.create_end_buffer();
      sycl::buffer<bool, 1> ret_type_sycl = host_data.create_ret_type_buffer();

      queue
          .submit([&](sycl::handler& cgh) {
            sycl::accessor<T, 1> ref_input_acc(ref_input_sycl, cgh);
            sycl::accessor<U, 1> res_acc(res_sycl, cgh);
            sycl::accessor<bool, 1> end_acc(end_sycl, cgh);
            sycl::accessor<bool, 1> ret_type_acc(ret_type_sycl, cgh);

            cgh.parallel_for<joint_scan_group_kernel<GroupT, T, U, I, OpT>>(
                executionRange, [=](sycl::nd_item<1> item) {
                  sycl::sub_group sub_group = item.get_sub_group();

                  // If this item is not participating in the group, leave
                  // early.
                  if (!NonUniformGroupHelper<GroupT>::should_participate(
                          sub_group, test_case))
                    return;

                  GroupT non_uniform_group =
                      NonUniformGroupHelper<GroupT>::create(sub_group,
                                                            test_case);

                  // Likewise, we only use a single group to compute these
                  // values.
                  if (sub_group.get_group_linear_id() != 0 ||
                      non_uniform_group.get_group_linear_id() !=
                          NonUniformGroupHelper<GroupT>::
                              preferred_single_worker_group_id(test_case))
                    return;

                  T* v_begin = ref_input_acc.get_pointer();
                  T* v_end = v_begin + ref_input_acc.size();

                  U* r_nug_e_begin = res_acc.get_pointer();
                  U* r_nug_i_begin = res_acc.get_pointer() + size;

                  auto r_nug_e_end = joint_exclusive_scan_helper<I>(
                      non_uniform_group, v_begin, v_end, r_nug_e_begin, op,
                      with_init);
                  ret_type_acc[0] = std::is_same_v<U*, decltype(r_nug_e_end)>;

                  auto r_nug_i_end = joint_inclusive_scan_helper<I>(
                      non_uniform_group, v_begin, v_end, r_nug_i_begin, op,
                      with_init);
                  ret_type_acc[1] = std::is_same_v<U*, decltype(r_nug_i_end)>;

                  end_acc[0] = (r_nug_e_begin + size == r_nug_e_end);
                  end_acc[1] = (r_nug_i_begin + size == r_nug_i_end);
                });
          })
          .wait_and_throw();
    }

    host_data.check_results(size, op, op_name, group_name, with_init);
  }
}

/**
 * @brief Provides test for joint scans
 * @tparam GroupT Group type to test with
 * @tparam T Type pointed by InPtr
 * @tparam U Type pointed by OutPtr
 * @tparam OperatorT Type of binary operation
 */
template <typename GroupT, typename T, typename U, typename OperatorT>
struct joint_scan_group {
  void operator()(sycl::queue& queue, const std::string& op_name) {
    if constexpr (type_traits::group_algorithms::is_legal_operator_v<
                      U, OperatorT>) {
      INFO(" with types " + type_name<T>() + " and " + type_name<U>());

      sycl::range<1> work_group_range =
          sycl_cts::util::work_group_range<1>(queue, test_size);

      size_t work_group_size = work_group_range.size();

      sycl::nd_range<1> executionRange(work_group_range, work_group_range);

      const size_t sizes[2] = {5, 2};
      for (size_t size : sizes) {
        check_scan<GroupT, T, U>(queue, size, executionRange, OperatorT(),
                                 op_name, false);
      }
    }
  }
};

template <typename GroupT, typename T, typename U>
class invoke_joint_scan_group {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<U>();
    for_all_combinations<joint_scan_group, GroupT, T, U>(operators, queue);
  }
};

// FIXME: Helper for implementations that cannot handle cases of different types
template <typename GroupT, typename T>
class invoke_joint_scan_group_same_type {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<T>();
    for_all_combinations<joint_scan_group, GroupT, T, T>(operators, queue);
  }
};

template <typename GroupT, typename T, typename U, typename I>
class init_joint_scan_group_kernel;

/**
 * @brief Provides test for joint scans with init
 * @tparam GroupT Group type to test with
 * @tparam T Type pointed by InPtr
 * @tparam U Type pointed by OutPtr
 * @tparam I Type used for init value
 * @tparam OperatorT Type of binary operation
 */
template <typename GroupT, typename T, typename U, typename I,
          typename OperatorT>
struct init_joint_scan_group {
  void operator()(sycl::queue& queue, const std::string& op_name) {
    if constexpr (type_traits::group_algorithms::is_legal_operator_v<
                      I, OperatorT>) {
      INFO(" with types " + type_name<T>() + " and " + type_name<U>() +
           ", init type " + type_name<I>());

      sycl::range<1> work_group_range =
          sycl_cts::util::work_group_range<1>(queue, test_size);
      sycl::nd_range<1> executionRange(work_group_range, work_group_range);

      size_t work_group_size = work_group_range.size();

      const size_t sizes[2] = {5, 2};
      for (size_t size : sizes) {
        check_scan<GroupT, T, U, I>(queue, size, executionRange, OperatorT(),
                                    op_name, true);
      }
    }
  }
};

template <typename GroupT, typename T, typename U, typename I>
class invoke_init_joint_scan_group {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<I>();
    for_all_combinations<init_joint_scan_group, GroupT, T, U, I>(operators,
                                                                 queue);
  }
};

// FIXME: Helper for implementations that cannot handle cases of different types
template <typename GroupT, typename T>
class invoke_init_joint_scan_group_same_type {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<T>();
    for_all_combinations<init_joint_scan_group, GroupT, T, T, T>(operators,
                                                                 queue);
  }
};

template <typename GroupT, typename T, typename U, typename OpT>
class scan_over_group_kernel;

template <typename T, typename U, typename Group, typename OpT>
auto inclusive_scan_over_group_helper(Group group, U x, OpT op,
                                      bool with_init) {
  if (with_init) {
    return sycl::inclusive_scan_over_group(group, x, op, T(init));
  }
  assert((std::is_same_v<T, U> &&
          "Without init value T and U should be the same type."));
  return sycl::inclusive_scan_over_group(group, T(x), op);
}

template <typename T, typename U, typename Group, typename OpT>
auto exclusive_scan_over_group_helper(Group group, U x, OpT op,
                                      bool with_init) {
  if (with_init) {
    return sycl::exclusive_scan_over_group(group, x, T(init), op);
  }
  assert((std::is_same_v<T, U> &&
          "Without init value T and U should be the same type."));
  return sycl::exclusive_scan_over_group(group, T(x), op);
}

template <typename T, typename U>
struct ScanOverGroupDataStruct {
  ScanOverGroupDataStruct(size_t range_size)
      : ref_input(range_size),
        res(range_size * 2, T(-1)),
        local_id(range_size, 0),
        sub_group_id(range_size, 0),
        non_uniform_group_id(range_size, 0) {
    std::iota(ref_input.begin(), ref_input.end(), U(1));
    std::fill(local_id.begin(), local_id.end(),
              std::numeric_limits<size_t>::max());
    std::fill(sub_group_id.begin(), sub_group_id.end(),
              std::numeric_limits<size_t>::max());
    std::fill(non_uniform_group_id.begin(), non_uniform_group_id.end(),
              std::numeric_limits<size_t>::max());
  }

  template <typename OpT>
  void check_results(size_t range_size, OpT op, const std::string& op_name,
                     const std::string& group_name, bool with_init) {
    CHECK(ret_type[0]);
    CHECK(ret_type[1]);

    T init_value = with_init ? T(init) : sycl::known_identity<OpT, T>::value;
    {
      // Mapping from "sub-group id" and "non-uniform group id" to "vector of
      // input data (ordered by item linear id within the sub-group)"
      std::map<std::pair<size_t, size_t>, std::vector<T>> ref_input_per_group;
      for (int i = 0; i < range_size; i++) {
        size_t sgid = sub_group_id[i];
        // If sub-group id is max value it was an item not participating, so
        // skip it.
        if (sgid == std::numeric_limits<size_t>::max()) continue;

        size_t nugid = non_uniform_group_id[i];
        auto key = std::make_pair(sgid, nugid);
        std::vector<T>& input_vec = ref_input_per_group[key];
        // Extend input vector dynamically.
        size_t lid = local_id[i];
        if (input_vec.size() <= lid) input_vec.resize(lid + 1);
        // Place the data identified by (sgid, lid).
        input_vec[lid] = ref_input[i];
      }
      // Compute the reference results and verify.
      for (int i = 0; i < range_size; i++) {
        size_t sgid = sub_group_id[i];
        // If sub-group id is max value it was an item not participating, so
        // skip it.
        if (sgid == std::numeric_limits<size_t>::max()) continue;

        size_t nugid = non_uniform_group_id[i];
        auto key = std::make_pair(sgid, nugid);
        const std::vector<T>& input_vec = ref_input_per_group[key];
        // Scan over the first (lid + 1) elements of input_vec to obtain the
        // result identified by i.
        size_t lid = local_id[i];
        std::vector<T> reference(lid + 1, T(-1));
        std::exclusive_scan(input_vec.begin(), input_vec.begin() + lid + 1,
                            reference.begin(), init_value, op);
        {
          int res_i = i;
          INFO("Check exclusive_scan_over_group on " + group_name +
               " for element " + std::to_string(i) + " (Operator: " + op_name +
               ")");
          INFO("Result: " + std::to_string(res[i]));
          INFO("Expected: " + std::to_string(reference[lid]));
          CHECK(res[i] == reference[lid]);
        }
        std::inclusive_scan(input_vec.begin(), input_vec.begin() + lid + 1,
                            reference.begin(), op, init_value);
        {
          INFO("Check inclusive_scan_over_group on " + group_name +
               " for element " + std::to_string(i) + " (Operator: " + op_name +
               ")");
          INFO("Result: " + std::to_string(res[range_size + i]));
          INFO("Expected: " + std::to_string(reference[lid]));
          CHECK(res[range_size + i] == reference[lid]);
        }
      }
    }
  }

  sycl::buffer<U, 1> create_ref_input_buffer() {
    return {ref_input.data(), ref_input.size()};
  }

  sycl::buffer<T, 1> create_res_buffer() { return {res.data(), res.size()}; }

  sycl::buffer<bool, 1> create_ret_type_buffer() { return {ret_type, 4}; }

  sycl::buffer<size_t, 1> create_local_id_buffer() {
    return {local_id.data(), local_id.size()};
  }

  sycl::buffer<size_t, 1> create_sub_group_id_buffer() {
    return {sub_group_id.data(), sub_group_id.size()};
  }

  sycl::buffer<size_t, 1> create_non_uniform_group_id_buffer() {
    return {non_uniform_group_id.data(), non_uniform_group_id.size()};
  }

  std::vector<U> ref_input;
  std::vector<T> res;
  bool ret_type[2] = {false, false};
  std::vector<size_t> local_id;
  std::vector<size_t> sub_group_id;
  std::vector<size_t> non_uniform_group_id;
};

template <typename GroupT, typename T, typename U = T, typename OpT>
void check_scan_over_group(sycl::queue& queue, sycl::range<1> range, OpT op,
                           const std::string& op_name, bool with_init) {
  const std::string group_name = NonUniformGroupHelper<GroupT>::get_name();
  auto range_size = range.size();

  for (size_t test_case = 0;
       test_case < NonUniformGroupHelper<GroupT>::num_test_cases; ++test_case) {
    const std::string test_case_name =
        NonUniformGroupHelper<GroupT>::get_test_case_name(test_case);
    INFO("Running test case (" + std::to_string(test_case) + ") with " +
         test_case_name);

    ScanOverGroupDataStruct<T, U> host_data{range_size};
    {
      auto ref_input_sycl = host_data.create_ref_input_buffer();
      auto res_sycl = host_data.create_res_buffer();
      auto ret_type_sycl = host_data.create_ret_type_buffer();
      auto local_id_sycl = host_data.create_local_id_buffer();
      auto sg_id_sycl = host_data.create_sub_group_id_buffer();
      auto nug_id_sycl = host_data.create_non_uniform_group_id_buffer();

      queue
          .submit([&](sycl::handler& cgh) {
            sycl::accessor<U, 1, sycl::access_mode::read> ref_input_acc(
                ref_input_sycl, cgh);
            sycl::accessor<T, 1> res_acc(res_sycl, cgh);
            sycl::accessor<bool, 1> ret_type_acc(ret_type_sycl, cgh);
            sycl::accessor<size_t, 1> local_id_acc(local_id_sycl, cgh);
            sycl::accessor<size_t, 1> sg_id_acc(sg_id_sycl, cgh);
            sycl::accessor<size_t, 1> nug_id_acc(nug_id_sycl, cgh);

            cgh.parallel_for<scan_over_group_kernel<GroupT, T, U, OpT>>(
                sycl::nd_range<1>(range, range), [=](sycl::nd_item<1> item) {
                  sycl::sub_group sub_group = item.get_sub_group();

                  // If this item is not participating in the group, leave
                  // early.
                  if (!NonUniformGroupHelper<GroupT>::should_participate(
                          sub_group, test_case))
                    return;

                  GroupT non_uniform_group =
                      NonUniformGroupHelper<GroupT>::create(sub_group,
                                                            test_case);

                  auto g_index = item.get_global_linear_id();

                  local_id_acc[g_index] =
                      non_uniform_group.get_local_linear_id();
                  sg_id_acc[g_index] = sub_group.get_group_linear_id();
                  nug_id_acc[g_index] = non_uniform_group.get_group_linear_id();

                  auto res_nug_e = exclusive_scan_over_group_helper<T>(
                      non_uniform_group, ref_input_acc[g_index], op, with_init);
                  res_acc[g_index] = res_nug_e;
                  ret_type_acc[0] = std::is_same_v<T, decltype(res_nug_e)>;

                  auto res_nug_i = inclusive_scan_over_group_helper<T>(
                      non_uniform_group, ref_input_acc[g_index], op, with_init);
                  res_acc[range_size + g_index] = res_nug_i;
                  ret_type_acc[1] = std::is_same_v<T, decltype(res_nug_i)>;
                });
          })
          .wait_and_throw();
    }

    host_data.check_results(range_size, op, op_name, group_name, with_init);
  }
}

/**
 * @brief Provides test for scans over group values
 * @tparam GroupT Group type to test with
 * @tparam T Type used for value
 * @tparam OperatorT Type of binary operation
 */
template <typename GroupT, typename T, typename OperatorT>
struct scan_over_group {
  void operator()(sycl::queue& queue, const std::string& op_name) {
    if constexpr (type_traits::group_algorithms::is_legal_operator_v<
                      T, OperatorT>) {
      INFO(" with type " + type_name<T>());

      sycl::range<1> work_group_range =
          sycl_cts::util::work_group_range<1>(queue, test_size);
      size_t work_group_size = work_group_range.size();

      check_scan_over_group<GroupT, T>(queue, work_group_range, OperatorT(),
                                       op_name, false);
    }
  }
};

template <typename GroupT, typename T>
class invoke_scan_over_group {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<T>();
    for_all_combinations<scan_over_group, GroupT, T>(operators, queue);
  }
};

template <typename GroupT, typename T, typename U>
class init_scan_over_group_kernel;

// many errors with short types for AdaptiveCpp
// it means conversion and calculation patterns are not OK
/**
 * @brief Provides test for scans over group with an init value
 * @tparam GroupT Group type to test with
 * @tparam T Type used for init value and result
 * @tparam U Type used for group values
 * @tparam OperatorT Type of binary operation
 */
template <typename GroupT, typename T, typename U, typename OperatorT>
struct init_scan_over_group {
  void operator()(sycl::queue& queue, const std::string& op_name) {
    if constexpr (type_traits::group_algorithms::is_legal_operator_v<
                      T, OperatorT>) {
      INFO(" with types " + type_name<T>() + " and " + type_name<U>());

      sycl::range<1> work_group_range =
          sycl_cts::util::work_group_range<1>(queue, test_size);

      check_scan_over_group<GroupT, T, U>(queue, work_group_range, OperatorT(),
                                          op_name, true);
    }
  }
};

template <typename GroupT, typename T, typename U>
class invoke_init_scan_over_group {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<T>();
    for_all_combinations<init_scan_over_group, GroupT, T, U>(operators, queue);
  }
};

// FIXME: Helper for implementations that cannot handle cases of different types
template <typename GroupT, typename T>
class invoke_init_scan_over_group_same_type {
 public:
  void operator()(sycl::queue& queue) {
    const auto operators = get_op_types<T>();
    for_all_combinations<init_scan_over_group, GroupT, T, T>(operators, queue);
  }
};
