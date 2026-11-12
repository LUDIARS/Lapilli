// lapilli::OrderedMap / OrderedSet iterate in insertion order, as JavaScript's Map and Set.
#include "lapilli/ordered.hpp"
#include "lapilli/test/check.hpp"

#include <string>

LAPILLI_TEST("Map#set keeps the first position, delete keeps the rest in order") {
    lapilli::OrderedMap<std::string, int> map{{"b", 1}, {"a", 2}};
    map.set("c", 3);
    map.set("b", 10);
    LAPILLI_CHECK((map.keys() == std::vector<std::string>{"b", "a", "c"}));
    LAPILLI_CHECK_EQ(*map.get("b"), 10);
    LAPILLI_CHECK(map.erase("a"));
    LAPILLI_CHECK(!map.erase("a"));
    LAPILLI_CHECK((map.keys() == std::vector<std::string>{"b", "c"}));
    LAPILLI_CHECK(!map.get_copy("a").has_value());
    LAPILLI_CHECK_EQ(map.size(), 2u);
}

LAPILLI_TEST("Set#add ignores duplicates and keeps insertion order") {
    lapilli::OrderedSet<int> set{3, 1, 3, 2};
    LAPILLI_CHECK((set.values() == std::vector<int>{3, 1, 2}));
    LAPILLI_CHECK(set.erase(1));
    set.add(1);
    LAPILLI_CHECK((set.values() == std::vector<int>{3, 2, 1}));
    LAPILLI_CHECK(set.has(2) && !set.has(9));
}
