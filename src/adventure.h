#ifndef SRC_ADVENTURE_H_
#define SRC_ADVENTURE_H_

#include <algorithm>
#include <random>
#include <vector>

#include "../third_party/threadpool/threadpool.h"

#include "./types.h"
#include "./utils.h"

static const size_t cacheline = 8;

// Function returns a random size_t variable between l and r
static inline size_t getDivisionPoint(size_t l, size_t r) {
  if (l < r) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(l, r);
    return dis(gen);
  } else {
    return l;
  }
}

// function sorts a vector of GrainOfSand using the quicksort algorithm
static inline void quickSort(std::vector<GrainOfSand> &grains, size_t l,
                             size_t r) {
  if (l < r) {
    std::swap(grains[r], grains[getDivisionPoint(l, r)]);

    GrainOfSand div = grains[r];
    size_t act = l;
    for (size_t i = l; i < r; ++i) {
      if (grains[i] < div) {
        std::swap(grains[i], grains[act++]);
      }
    }
    std::swap(grains[act], grains[r]);
    if (act != l) {
      quickSort(grains, l, act - 1);
    }
    if (act != r) {
      quickSort(grains, act + 1, r);
    }
  }
}

class Adventure {
 public:
  virtual ~Adventure() = default;

  virtual uint64_t packEggs(std::vector<Egg> eggs, BottomlessBag &bag) = 0;

  virtual void arrangeSand(std::vector<GrainOfSand> &grains) = 0;

  virtual Crystal selectBestCrystal(std::vector<Crystal> &crystals) = 0;
};

class LonesomeAdventure : public Adventure {
 private:
  static size_t max_ab(size_t a, size_t b) { return (a >= b) ? a : b; }

 public:
  LonesomeAdventure() {}

  // Function that fills the bag with eggs using the 0-1 knapsack algorithm
  virtual uint64_t packEggs(std::vector<Egg> eggs, BottomlessBag &bag) {
    size_t cap = bag.getCapacity();
    size_t sz = eggs.size();
    size_t **DP = new size_t *[sz + 1];
    for (size_t i = 0; i <= sz; ++i) {
      DP[i] = new size_t[cap + 1];
    }
    for (size_t i = 0; i <= cap; ++i) {
      DP[0][i] = 0;
    }

    for (size_t p = 1; p <= sz; ++p) {
      size_t s = eggs[p - 1].getSize();
      size_t w = eggs[p - 1].getWeight();
      for (size_t a = 0; a <= cap; ++a) {
        if (s > a) {
          DP[p][a] = DP[p - 1][a];
        } else {
          DP[p][a] = max_ab(w + DP[p - 1][a - s], DP[p - 1][a]);
        }
      }
    }
    size_t res = DP[sz][cap];
    size_t w = cap;
    for (size_t i = sz; i > 0 && res > 0; i--) {
      if (res != DP[i - 1][w]) {
        bag.addEgg(eggs[i - 1]);
        res = res - eggs[i - 1].getWeight();
        w = w - eggs[i - 1].getSize();
      }
    }
    std::cout << "";
    res = DP[sz][cap];
    for (size_t i = 0; i <= sz; ++i) {
      delete[] DP[i];
    }
    delete[] DP;
    return res;
  }

  virtual void arrangeSand(std::vector<GrainOfSand> &grains) {
    quickSort(grains, 0, grains.size() - 1);
  }

  // Function that returns the shiniest Crystal from the vector
  virtual Crystal selectBestCrystal(std::vector<Crystal> &crystals) {
    Crystal shiniest;
    for (Crystal c : crystals) {
      shiniest = (shiniest < c) ? c : shiniest;
    }
    return shiniest;
  }
};

class TeamAdventure : public Adventure {
 private:
  static size_t max_ab(size_t a, size_t b) { return (a >= b) ? a : b; }

  static Crystal maxCrystal(std::vector<Crystal> &crystals, size_t l,
                            size_t r) {
    Crystal shiniest;
    for (size_t i = l; i < crystals.size() && i < r; ++i) {
      shiniest = (shiniest < crystals[i]) ? crystals[i] : shiniest;
    }
    return shiniest;
  }

  // Function that waits on the barier and performs dynamic knapsack algorithm
  // on the same segment of every row
  static size_t segmentKnapsack(std::vector<Egg> &eggs, size_t **DP, size_t l,
                                size_t r, size_t cap, TeamAdventure &adv) {
    for (size_t p = 1; p <= eggs.size(); ++p) {
      size_t size = eggs[p - 1].getSize();
      size_t weight = eggs[p - 1].getWeight();
      {
        // simulation of the barier that resets itself
        std::unique_lock<std::mutex> lk{adv.mut};
        size_t old_gen = adv.gen;
        adv.counter--;
        if (adv.counter == 0) {
          adv.gen++;
          adv.counter = adv.workers;
          adv.cv.notify_all();
        } else {
          adv.cv.wait(lk, [&adv, old_gen] { return old_gen != adv.gen; });
        }
      }
      for (size_t i = l; i <= cap && i < r; ++i) {
        if (size > i) {
          DP[p][i] = DP[p - 1][i];
        } else {
          DP[p][i] = max_ab(weight + DP[p - 1][i - size], DP[p - 1][i]);
        }
      }
    }

    return 0;
  }

  // After partitioning the segment the function performs a recursive call of
  // itself on the lower and upper segments
  static void parQuickSort(std::vector<GrainOfSand> &grains, size_t l, size_t r,
                           ThreadPool &councilOfShamans, std::atomic_int &cnt,
                           std::mutex &m, std::condition_variable &cv) {
    if (l < r) {
      std::swap(grains[r], grains[getDivisionPoint(l, r)]);

      GrainOfSand div = grains[r];
      size_t act = l;
      for (size_t i = l; i < r; ++i) {
        if (grains[i] < div) {
          std::swap(grains[i], grains[act++]);
        }
      }
      std::swap(grains[act], grains[r]);
      // When a segment is too small to justify parallel sort, we use normal
      // quicksort
      if (act - l > cacheline) {
        std::lock_guard<std::mutex> lck(m);
        cnt++;
        councilOfShamans.enqueue(parQuickSort, std::ref(grains), l, act - 1,
                                 std::ref(councilOfShamans), std::ref(cnt),
                                 std::ref(m), std::ref(cv));
      } else if (act != l) {
        quickSort(grains, l, act - 1);
      }
      if (r - act > cacheline) {
        std::lock_guard<std::mutex> lck(m);
        cnt++;
        councilOfShamans.enqueue(parQuickSort, std::ref(grains), act + 1, r,
                                 std::ref(councilOfShamans), std::ref(cnt),
                                 std::ref(m), std::ref(cv));
      } else if (act != r) {
        quickSort(grains, act + 1, r);
      }
    }
    if (--cnt == 0) {
      m.lock();
      --cnt;
      cv.notify_one();
      m.unlock();
    }
  }

 public:
  explicit TeamAdventure(uint64_t numberOfShamansArg)
      : numberOfShamans(numberOfShamansArg),
        councilOfShamans(numberOfShamansArg) {}

  uint64_t packEggs(std::vector<Egg> eggs, BottomlessBag &bag) {
    std::mutex m;
    size_t cap = bag.getCapacity();
    size_t sz = eggs.size();
    size_t **DP = new size_t *[sz + 1];
    for (size_t i = 0; i <= sz; ++i) {
      DP[i] = new size_t[cap + 1];
    }
    for (size_t i = 0; i <= cap; ++i) {
      DP[0][i] = 0;
    }
    size_t jump = (cap + 1) / numberOfShamans + 1;
    if (jump <= cacheline) {
      jump = cacheline;
    }
    workers = (cap + 1) / jump;
    workers += ((cap + 1) % jump == 0) ? 0 : 1;
    counter = workers;
    gen = 0;
    std::vector<std::future<size_t>> futureResults;
    for (size_t i = 0; i <= cap; i += jump) {
      {
        std::lock_guard<std::mutex> lck(m);
        futureResults.push_back(councilOfShamans.enqueue(
            segmentKnapsack, eggs, DP, i, i + jump + 1, cap, std::ref(*this)));
      }
    }
    for (size_t i = futureResults.size(); i > 0; --i) {
      futureResults[i - 1].get();
      futureResults.pop_back();
    }

    size_t res = DP[sz][cap];
    size_t w = cap;
    for (size_t i = sz; i > 0 && res > 0; i--) {
      if (res != DP[i - 1][w]) {
        bag.addEgg(eggs[i - 1]);
        res = res - eggs[i - 1].getWeight();
        w = w - eggs[i - 1].getSize();
      }
    }
    std::cout << "";
    res = DP[sz][cap];
    for (size_t i = 0; i <= sz; ++i) {
      delete[] DP[i];
    }
    delete[] DP;
    return res;
  }

  virtual void arrangeSand(std::vector<GrainOfSand> &grains) {
    std::mutex m;
    std::unique_lock<std::mutex> lck(m);

    std::atomic_int cnt = 1;
    councilOfShamans.enqueue(parQuickSort, std::ref(grains), 0,
                             grains.size() - 1, std::ref(councilOfShamans),
                             std::ref(cnt), std::ref(m), std::ref(cv));
    cv.wait(lck, [&cnt] { return cnt == -1; });
  }

  // We split the vector into roughly equal segments and assign those segments
  // to threadpool
  virtual Crystal selectBestCrystal(std::vector<Crystal> &crystals) {
    size_t jump = crystals.size() / numberOfShamans;
    jump = (jump < cacheline) ? cacheline : jump;
    std::vector<std::future<Crystal>> futureResults;
    for (size_t i = 0; i < crystals.size(); i += jump + 1) {
      futureResults.push_back(councilOfShamans.enqueue(
          maxCrystal, std::ref(crystals), i, i + jump + 1));
    }
    Crystal result;
    for (size_t i = 0; i < futureResults.size(); ++i) {
      Crystal tmp = futureResults[i].get();
      result = (result < tmp) ? tmp : result;
    }
    return result;
  }

 private:
  uint64_t numberOfShamans;
  ThreadPool councilOfShamans;
  std::mutex mut;
  std::condition_variable cv;
  size_t counter;
  size_t workers;
  size_t gen;
};

#endif  // SRC_ADVENTURE_H_
