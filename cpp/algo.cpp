//
//  algo.cpp
//  Simdex
//
//

#include "algo.hpp"
#include "utils.hpp"
#include "parser.hpp"
#include "arith.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <queue>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <numeric>

#include <mkl.h>
#include <ipps.h>

std::vector<float> linspace(const float start, const float end, const int num) {
  float delta = (end - start) / num;

  std::vector<float> linspaced;
  // start from 1; omit start (we only care about upper bounds)
  for (int i = 0; i < num; ++i) {
    linspaced.push_back(start + delta * (i + 1));
  }
  return linspaced;
}

/**
 * Find index of smallest theta_b that is greater than theta_uc,
 * so we can find the right list of sorted upper bounds for a given
 * user
 **/
int find_theta_bin_index(const float theta_uc,
                         const std::vector<float> theta_bins,
                         const int num_bins) {
  for (int i = 0; i < num_bins; ++i) {
    if (theta_uc <= theta_bins[i]) {
      return i;
    }
  }
  return num_bins - 1;
}

#ifdef DEBUG
void check_against_naive(const float *user_weight, const float *item_weights,
                         const int num_items, const int num_latent_factors,
                         const int *computed_top_K,
                         const float *computed_scores, const int K) {
  const int m = num_items;
  const int k = num_latent_factors;

  const float alpha = 1.0;
  const float beta = 0.0;
  const int stride = 1;

  float *scores = (float *)_malloc(sizeof(float) * num_items);
  cblas_sgemv(CblasRowMajor, CblasNoTrans, m, k, alpha, item_weights, k,
              user_weight, stride, beta, scores, stride);

  std::vector<int> v(num_items);
  std::iota(v.begin(), v.end(), 0);
  std::sort(v.begin(), v.end(),
            [&scores](int i1, int i2) { return scores[i1] > scores[i2]; });
  for (int i = 0; i < K; ++i) {
    if (v[i] != computed_top_K[i]) {
      std::cout << "v[i] does not equal computed_top_K[i], i = " << i
                << std::endl;
      exit(1);
    }
  }
  _free(scores);
}
#endif

void computeTopKForCluster(const int cluster_id, const float *centroid,
                           const std::vector<int> &user_ids_in_cluster,
                           const float *user_weights, const float *item_weights,
                           const float *item_norms, const float *theta_ics,
                           const int num_items, const int num_latent_factors,
                           const int num_bins, const int K,
                           std::ofstream &user_stats_file) {

  double time_start, time_end, upperBoundCreation_time, sortUpperBound_time,
      computeTopK_time;

  const int num_users_in_cluster = user_ids_in_cluster.size();

  userNormTuple_t userNormTuple_array[num_users_in_cluster];
  // initialize userNormTuple_array with user ids that are assigned to this
  // cluster

  time_start = dsecnd();
  time_start = dsecnd();

  // compute theta_ucs for ever user assigned to this cluster,
  // and also initialize userNormTuple_array with the norms of every user
  // NOTE: theta_ucs are now already in the right order, i.e., you can access
  // them sequentially. This is because we reordered the user weights to be
  // in cluster order
  float *theta_ucs = compute_theta_ucs_for_centroid(
      user_weights, centroid, num_users_in_cluster, num_latent_factors,
      userNormTuple_array);

  float theta_max = theta_ucs[cblas_isamax(num_users_in_cluster, theta_ucs, 1)];
  // if (isnan(theta_max) != 0) {
  //   theta_max = 0;
  //   num_bins = 1;
  //   std::cout << "NaN detected." << std::endl;
  // }

  const std::vector<float> theta_bins = linspace(0.F, theta_max, num_bins);
  // theta_bins is correct
  float temp_upper_bounds[num_items];
  float upper_bounds[num_bins][num_items];

  int i, j;
  for (i = 0; i < num_bins; i++) {
    // TODO: inefficient copy value to all items in the array
    for (j = 0; j < num_items; j++) {
      temp_upper_bounds[j] = theta_bins[i];
    }
    // temp_upper_bounds = theta_b
    vsSub(num_items, theta_ics, temp_upper_bounds, temp_upper_bounds);
    // temp_upper_bounds = theta_ic - theta_b
    for (int l = 0; l < num_items; ++l) {
      // TODO: inefficient
      if (temp_upper_bounds[l] < 0) {
        temp_upper_bounds[l] = 0.F;
      }
    }
    vsCos(num_items, temp_upper_bounds, temp_upper_bounds);
    // temp_upper_bounds = cos(theta_ic - theta_b)
    vsMul(num_items, item_norms, temp_upper_bounds, upper_bounds[i]);
    // upper_bounds[i] = ||i|| * cos(theta_ic - theta_b)
  }

  // upper_bounds are correct
  time_end = dsecnd();
  upperBoundCreation_time = (time_end - time_start);

  time_start = dsecnd();
  time_start = dsecnd();

  // upperBoundItem_t **sorted_upper_bounds =
  //     (upperBoundItem_t **)_malloc(num_bins * sizeof(upperBoundItem_t *));
  upperBoundItem_t sorted_upper_bounds[num_bins][num_items];
  for (i = 0; i < num_bins; i++) {
    // sorted_upper_bounds[i] =
    //     (upperBoundItem_t *)_malloc(num_items * sizeof(struct
    // upperBoundItem));
    for (j = 0; j < num_items; j++) {
      sorted_upper_bounds[i][j].upperBound = upper_bounds[i][j];
      sorted_upper_bounds[i][j].itemID = j;
    }
  }
  IppSizeL *pBufSize = (IppSizeL *)malloc(sizeof(IppSizeL));
  ippsSortRadixGetBufferSize_L(num_items, ipp64s, pBufSize);
  Ipp8u *pBuffer = (Ipp8u *)malloc(*pBufSize * sizeof(Ipp8u));
  for (i = 0; i < num_bins; i++) {
    ippsSortRadixDescend_64s_I_L((Ipp64s *)sorted_upper_bounds[i], num_items,
                                 pBuffer);
  }
  // sorted_upper_bounds are correct

  time_end = dsecnd();
  sortUpperBound_time = (time_end - time_start);

  // ----------Computer Per User TopK Below------------------
  int top_K_items[num_users_in_cluster][K];

  time_start = dsecnd();
  time_start = dsecnd();

#ifdef DEBUG
  const int num_users_to_compute =
      num_users_in_cluster < 10 ? num_users_in_cluster : 10;
#else
  const int num_users_to_compute = num_users_in_cluster;
#endif
  for (i = 0; i < num_users_to_compute; i++) {
    const int bin_index =
        find_theta_bin_index(theta_ucs[i], theta_bins, num_bins);

    std::priority_queue<std::pair<float, int>,
                        std::vector<std::pair<float, int> >,
                        std::greater<std::pair<float, int> > > q;

    float score = 0.F;
    int itemID = 0;

    for (j = 0; j < K; j++) {
      itemID = sorted_upper_bounds[bin_index][j].itemID;
      score = cblas_sdot(num_latent_factors,
                         &item_weights[itemID * num_latent_factors], 1,
                         &user_weights[i * num_latent_factors], 1);
      q.push(std::make_pair(score, itemID));
    }
    int num_items_visited = K;

    for (j = K; j < num_items; j++) {
      if (q.top().first > (userNormTuple_array[i].userNorm *
                           sorted_upper_bounds[bin_index][j].upperBound)) {
        break;
      }
      itemID = sorted_upper_bounds[bin_index][j].itemID;
      score = cblas_sdot(num_latent_factors,
                         &item_weights[itemID * num_latent_factors], 1,
                         &user_weights[i * num_latent_factors], 1);
      num_items_visited++;
      if (q.top().first < score) {
        q.pop();
        q.push(std::make_pair(score, itemID));
      }
    }

#ifdef DEBUG
    float top_K_scores[K];
#endif
    for (j = 0; j < K; j++) {
      std::pair<float, int> p = q.top();
#ifdef DEBUG
      top_K_scores[K - 1 - j] = p.first;
#endif
      // don't need to store score
      top_K_items[i][K - 1 - j] = p.second;  // store item ID
      q.pop();
    }

#ifdef DEBUG
    std::cout << "User ID " << user_ids_in_cluster[i] << std::endl;
    check_against_naive(&user_weights[i * num_latent_factors], item_weights,
                        num_items, num_latent_factors, top_K_items[i],
                        top_K_scores, K);
#endif

    user_stats_file << user_ids_in_cluster[i] << "," << cluster_id << ","
                    << theta_ucs[i] << "," << num_items_visited << std::endl;
  }

  time_end = dsecnd();
  computeTopK_time = (time_end - time_start);

  // ----------Free Allocated Memory Below-------

  MKL_free(theta_ucs);
  MKL_Free_Buffers();

  // printf("upper bound time: %f secs \n", upperBoundCreation_time);
  // printf("sort time: %f secs \n", sortUpperBound_time);
  // printf("compute top K time: %f secs \n", computeTopK_time);
  // creationTime += upperBoundCreation_time;
  // sortTime += sortUpperBound_time;
  // computeKTime += computeTopK_time;
}