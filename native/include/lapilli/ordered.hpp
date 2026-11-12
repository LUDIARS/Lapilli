// Insertion-ordered Map and Set, as JavaScript's Map and Set iterate. Lookups are linear in the
// worst case through an index map; iteration is in insertion order; re-setting an existing key
// keeps its position (Map#set semantics); erase keeps the order of the rest.
#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace lapilli {

template <class K, class V>
class OrderedMap {
public:
    using value_type = std::pair<K, V>;
    using const_iterator = typename std::vector<value_type>::const_iterator;
    using iterator = typename std::vector<value_type>::iterator;

    OrderedMap() = default;
    OrderedMap(std::initializer_list<value_type> items) {
        for (const auto& item : items) set(item.first, item.second);
    }

    void set(const K& key, V value) {
        auto found = index_.find(key);
        if (found != index_.end()) {
            items_[found->second].second = std::move(value);
            return;
        }
        index_.emplace(key, items_.size());
        items_.emplace_back(key, std::move(value));
    }
    [[nodiscard]] bool has(const K& key) const { return index_.count(key) > 0; }
    [[nodiscard]] const V* get(const K& key) const {
        auto found = index_.find(key);
        return found == index_.end() ? nullptr : &items_[found->second].second;
    }
    [[nodiscard]] V* get(const K& key) {
        auto found = index_.find(key);
        return found == index_.end() ? nullptr : &items_[found->second].second;
    }
    [[nodiscard]] std::optional<V> get_copy(const K& key) const {
        const V* value = get(key);
        return value == nullptr ? std::nullopt : std::optional<V>(*value);
    }
    bool erase(const K& key) {
        auto found = index_.find(key);
        if (found == index_.end()) return false;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(found->second));
        reindex();
        return true;
    }
    void clear() {
        items_.clear();
        index_.clear();
    }
    [[nodiscard]] std::size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end() const { return items_.end(); }
    iterator begin() { return items_.begin(); }
    iterator end() { return items_.end(); }
    [[nodiscard]] std::vector<K> keys() const {
        std::vector<K> out;
        out.reserve(items_.size());
        for (const auto& item : items_) out.push_back(item.first);
        return out;
    }
    friend bool operator==(const OrderedMap& a, const OrderedMap& b) { return a.items_ == b.items_; }

private:
    void reindex() {
        index_.clear();
        for (std::size_t i = 0; i < items_.size(); ++i) index_.emplace(items_[i].first, i);
    }
    std::vector<value_type> items_;
    std::map<K, std::size_t> index_;
};

template <class T>
class OrderedSet {
public:
    using const_iterator = typename std::vector<T>::const_iterator;

    OrderedSet() = default;
    OrderedSet(std::initializer_list<T> items) {
        for (const auto& item : items) add(item);
    }
    template <class It>
    OrderedSet(It first, It last) {
        for (; first != last; ++first) add(*first);
    }

    void add(const T& value) {
        if (index_.count(value) > 0) return;
        index_.emplace(value, items_.size());
        items_.push_back(value);
    }
    [[nodiscard]] bool has(const T& value) const { return index_.count(value) > 0; }
    bool erase(const T& value) {
        auto found = index_.find(value);
        if (found == index_.end()) return false;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(found->second));
        index_.clear();
        for (std::size_t i = 0; i < items_.size(); ++i) index_.emplace(items_[i], i);
        return true;
    }
    [[nodiscard]] std::size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end() const { return items_.end(); }
    [[nodiscard]] const std::vector<T>& values() const { return items_; }
    friend bool operator==(const OrderedSet& a, const OrderedSet& b) { return a.items_ == b.items_; }

private:
    std::vector<T> items_;
    std::map<T, std::size_t> index_;
};

} // namespace lapilli
