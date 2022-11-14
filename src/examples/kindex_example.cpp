#include <iostream>

#include <kindex.h>

using namespace kindex;

class Assignment
{
public:
    template <typename TriggerFunc>
    void trigger(TriggerFunc&& t) const
    {
        int va = 3;
        std::vector<std::string> vb = { "x", "y", "z" };
        
        t("a", &va, &va + 1);
        t("b", vb.begin(), vb.end());
    }

    size_t size() const { return 2; }

};

int main(int argc, char* argv[])
{
    using indexer = Indexer<std::string, Assignment>;
    using document = indexer::document_type;
    using conjunction = indexer::conjunction_type;
    using expression = indexer::expression_type;

    expression expr;
    expr.key = "a";
    expr.values = std::vector<int64_t>{ 3 };
    expr.positive = true;
    conjunction c;
    c.expressions.push_back(expr);
    document d;
    d.conjunctions.push_back(c);
    std::vector<document> docs;
    docs.push_back(d);
    indexer idx = indexer::create(docs);

    ResultSet result;
    Assignment s;
    idx.retrieve(result, s);

    for (auto& i : result.result_) {
        std::cout << "retrieve doc: " << i << std::endl;
    }

    return 0;
}
