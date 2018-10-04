#ifndef VOSE_ALIAS_H_
#define VOSE_ALIAS_H_

#include <map>
#include <vector>

template<typename T>
class VoseAlias {
 public:
  VoseAlias() { }

  VoseAlias(const std::map<T,double> &probs) {
    init(probs);
  }

  void init(const std::map<T,double> &probs) {
    size_t n = probs.size();
    std::map<T,double> scaled_prob;
    std::vector<T> large;
    std::vector<T> small;

    for(const auto &it : probs) {
      scaled_prob[it.first] = n * it.second;
      if (scaled_prob[it.first] < 1) {
	small.push_back(it.first);
      } else {
	large.push_back(it.first);
      }
    }

    while (!small.empty() && !large.empty()){
      T s = small.back();
      small.pop_back();
      T l = large.back();
      large.pop_back();

      prob_table_[s] = scaled_prob[s];
      alias_table_[s] = l;

      scaled_prob[l] = (scaled_prob[l] + scaled_prob[s]) - 1;

      if (scaled_prob[l] < 1) {
	small.push_back(l);
      } else {
	large.push_back(l);
      }
    }

    while (!large.empty()) {
      T l = large.back();
      prob_table_[l] = 1;
      large.pop_back();
    }

    while (!small.empty()) {
      T s = small.back();
      prob_table_[s] = 1;
      small.pop_back();
    }

    for(const auto &it : prob_table_) {
      keys.push_back(it.first);
    }
    table_len = prob_table_.size();
  };

  T generate_sample(const double &rnd_real1, const double &rnd_real2){
    T key = keys[(rnd_real1 * table_len)];
    if (rnd_real2 < prob_table_[key]) {
      return key;
    }
    return alias_table_[key];
  };

 private:
  size_t table_len;
  std::map<T, double> prob_table_;
  std::map<T, T> alias_table_;
  std::vector<T> keys;
};

#endif // VOSE_ALIAS_H_
