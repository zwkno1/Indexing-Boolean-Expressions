#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace detail {

using EntryId = std::uint64_t;

class Entry
{
public:
    Entry(EntryId docId, EntryId index, bool positive)
      : value_((docId << 17) | (index << 1) | (positive ? EntryId{ 1 } : EntryId{ 0 }))
    {
    }

    inline EntryId id() const { return value_ >> 1; }

    inline EntryId documentId() const { return value_ >> 17; }

    inline EntryId conjunctionIndex() const { return (value_ >> 1) & 0xFFFF; }

    inline bool isNegative() const { return !(value_ & 1); }

    inline bool operator==(Entry other) const { return value_ == other.value_; }

    inline bool operator<(Entry other) const { return value_ < other.value_; }

    inline constexpr static Entry max() { return Entry{ std::numeric_limits<EntryId>::max() }; }

private:
    constexpr Entry(EntryId id)
      : value_(id)
    {
    }

    EntryId value_;
};

class PostingList
{
public:
    PostingList(const Entry* begin, const Entry* end)
      : current_(begin)
      , end_(end)
    {
    }

    PostingList()
      : current_(nullptr)
      , end_(nullptr)
    {
    }

    inline bool empty() const { return current_ == end_; }

    inline const Entry current() const { return *current_; }

    inline void skipTo(EntryId id)
    {
        while ((current_ != end_) && (current().id() < id)) {
            ++current_;
        }
    }

private:
    const Entry* current_;

    const Entry* end_;
};

class PostingListGroup
{
public:
    PostingListGroup()
      : current_(Entry::max())
    {
    }

    inline bool operator<(const PostingListGroup& other) const { return current() < other.current(); }

    void add(PostingList plist)
    {
        if (plist.empty()) {
            return;
        }

        current_ = std::min(current_, plist.current());

        plists_.push_back(plist);
    }

    inline bool empty() const { return current_ == Entry::max(); }

    inline const Entry current() const { return current_; }

    inline void skipTo(EntryId id)
    {
        if (current_ == Entry::max()) {
            return;
        }

        Entry min = Entry::max();
        for (auto& plist : plists_) {
            if (plist.empty()) {
                continue;
            }
            plist.skipTo(id);
            if (plist.empty()) {
                continue;
            }
            if (plist.current() < min) {
                min = plist.current();
            }
        }

        current_ = min;
    }

private:
    Entry current_;

    std::vector<PostingList> plists_;
};

template <typename Key, typename T>
class InvertedIndexImpl
{
public:
    template <typename Iter>
    void addEntry(Entry entry, const Key& key, Iter beg, Iter end)
    {
        auto& t = indexs_[key];
        for (; beg != end; ++beg) {
            t[*beg].push_back(entry);
        }
    }

    template <typename Iter>
    void trigger(PostingListGroup& group, const Key& key, Iter beg, Iter end) const
    {
        auto iter = indexs_.find(key);
        if (iter == indexs_.end()) {
            return;
        }

        for (; beg != end; ++beg) {
            auto iter2 = iter->second.find(*beg);
            if (iter2 == iter->second.end()) {
                continue;
            }
            group.add(PostingList{ iter2->second.data(), iter2->second.data() + iter2->second.size() });
        }
    }

    void build()
    {
        for (auto& i : indexs_) {
            for (auto& j : i.second) {
                std::sort(j.second.begin(), j.second.end());
            }
        }
    }

private:
    std::unordered_map<Key, std::unordered_map<T, std::vector<Entry>>> indexs_;
};

template <typename Key>
class InvertedIndex
{
public:
    template <typename Iter>
    inline void addEntry(Entry entry, const Key& key, Iter beg, Iter end)
    {
        using value_type = std::remove_cv_t<std::remove_reference_t<decltype(*beg)>>;

        static_assert(std::is_integral_v<value_type> || std::is_same_v<value_type, std::string>, "unsupport type");

        if constexpr (std::is_same_v<value_type, std::string>) {
            stringIndex_.addEntry(entry, key, beg, end);
        } else if constexpr (std::is_integral_v<value_type>) {
            intIndex_.addEntry(entry, key, beg, end);
        }
    }

    template <typename Iter>
    void trigger(PostingListGroup& group, const Key& key, Iter beg, Iter end) const
    {
        using value_type = std::remove_cv_t<std::remove_reference_t<decltype(*beg)>>;

        static_assert(std::is_integral_v<value_type> || std::is_same_v<value_type, std::string>, "unsupport type");

        if constexpr (std::is_same_v<value_type, std::string>) {
            stringIndex_.trigger(group, key, beg, end);
        } else if constexpr (std::is_integral_v<value_type>) {
            intIndex_.trigger(group, key, beg, end);
        }
    }

    void build()
    {
        intIndex_.build();
        stringIndex_.build();
    }

private:
    InvertedIndexImpl<Key, int64_t> intIndex_;
    InvertedIndexImpl<Key, std::string> stringIndex_;
};

} // namespace detail

template <typename Key>
struct Expression
{
    Key key;
    std::variant<std::vector<std::string>, std::vector<int64_t>> values;
    bool positive;
};

template <typename Key>
struct Conjunction
{
    std::vector<Expression<Key>> expressions;
};

template <typename Key>
struct Document
{
    std::vector<Conjunction<Key>> conjunctions;
};

template <typename Key>
inline size_t getConjunctionSize(const Conjunction<Key>& c)
{
    return std::count_if(c.expressions.begin(), c.expressions.end(),
                         [](const Expression<Key>& e) { return e.positive; });
}

class ResultSet
{
public:
    inline void addDocumentId(uint64_t id) { result_.insert(id); }

    std::unordered_set<uint64_t> result_;
};

template <typename Key, typename Assignment>
class Indexer
{
public:
    using expression_type = Expression<Key>;
    using conjunction_type = Conjunction<Key>;
    using document_type = Document<Key>;

    void retrieve(ResultSet& result, const Assignment& s) const
    {
        std::vector<detail::PostingListGroup> plists;
        for (int i = std::min<int>(indexs_.size() - 1, s.size()); i >= 0; --i) {
            size_t k = i;

            plists.clear();
            getPostingLists(plists, k, s);

            if (k == 0) {
                k = 1;
            }

            if (plists.size() < k) {
                continue;
            }

            for (;;) {
                std::sort(plists.begin(), plists.end());

                if (plists[k - 1].empty()) {
                    break;
                }

                uint64_t nextId = 0;
                if (plists[0].current().id() == plists[k - 1].current().id()) {
                    if (plists[0].current().isNegative()) {
                        auto rejectId = plists[0].current().id();
                        for (size_t l = k; l < plists.size(); ++l) {
                            if (plists[l].current().id() == rejectId) {
                                plists[l].skipTo(rejectId + 1);
                            } else {
                                break;
                            }
                        }
                        // continue;
                    } else {
                        auto e = plists[k - 1].current();
                        auto docId = e.documentId();
                        result.addDocumentId(docId);
                    }
                    nextId = plists[k - 1].current().id() + 1;
                } else {
                    nextId = plists[k - 1].current().id();
                }

                for (size_t l = 0; l < k; ++l) {
                    plists[l].skipTo(nextId);
                }
            }
        }
    }

    inline static Indexer create(const std::vector<document_type>& documents)
    {
        Indexer indexer;
        indexer.build(documents);
        return indexer;
    }

private:
    void build(const std::vector<document_type>& documents)
    {
        for (uint64_t i = 0; i < documents.size(); ++i) {
            auto& doc = documents[i];

            if (doc.conjunctions.empty()) {
                continue;
            }

            for (uint64_t j = 0; j < (uint64_t)doc.conjunctions.size(); ++j) {
                auto& conjunction = doc.conjunctions[j];
                size_t size = getConjunctionSize(conjunction);
                for (auto& expr : conjunction.expressions) {
                    detail::Entry entry{ i, j, expr.positive };
                    if (indexs_.size() < size + 1) {
                        indexs_.resize(size + 1);
                    }
                    std::visit([&](auto&& v) { indexs_[size].addEntry(entry, expr.key, v.begin(), v.end()); },
                               expr.values);
                }

                if (size == 0) {
                    z_.push_back(detail::Entry{ i, j, true });
                }
            }
        }

        for (auto& i : indexs_) {
            i.build();
        }
        std::sort(z_.begin(), z_.end());
    }

private:
    inline void getPostingLists(std::vector<detail::PostingListGroup>& result, size_t k, const Assignment& s) const
    {
        s.trigger([&](const Key& key, auto beg, auto end) {
            detail::PostingListGroup group;
            indexs_[k].trigger(group, key, beg, end);
            if (!group.empty()) {
                result.push_back(std::move(group));
            }
        });

        if ((k == 0) && (!z_.empty())) {
            detail::PostingListGroup z;
            z.add(detail::PostingList{ z_.data(), z_.data() + z_.size() });
            result.push_back(z);
        }
    }

    std::vector<detail::InvertedIndex<Key>> indexs_;

    std::vector<detail::Entry> z_;
};
