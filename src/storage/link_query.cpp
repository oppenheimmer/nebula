#include "nebula/storage/workspace.hpp"
#include "records_internal.hpp"
namespace nebula {
std::string storage_detail::link_predicate(const std::vector<LinkFilter>& filters)
{
    validate_link_filters(filters); std::string result;
    for (const auto& filter:filters) {
        if (filter.relation==LinkRelation::ReferencesPage)
            result+=" AND EXISTS(SELECT 1 FROM links l WHERE l.source_page=p.id AND ((l.target_kind=0 AND l.target_id=?) OR (l.target_kind=1 AND l.target_id IN(SELECT id FROM blocks WHERE page_id=?))))";
        else result+=" AND EXISTS(SELECT 1 FROM links l WHERE l.source_page=? AND ((l.target_kind=0 AND l.target_id=p.id) OR (l.target_kind=1 AND l.target_id IN(SELECT id FROM blocks WHERE page_id=p.id))))";
    }
    return result;
}
void storage_detail::bind_link_filters(sql::Statement& query,int first,const std::vector<LinkFilter>& filters)
{
    for (const auto& filter:filters) {
        query.bind(first++,filter.page_id);
        if (filter.relation==LinkRelation::ReferencesPage) query.bind(first++,filter.page_id);
    }
}
std::vector<PageRecord> Workspace::query_view(const View& view) const
{
    const auto command="SELECT p.id FROM pages p JOIN databases d ON d.id=p.database_id WHERE p.database_id=? AND NOT p.trashed AND NOT d.trashed"+storage_detail::link_predicate(view.link_filters);
    sql::Statement query(database_.get(),command.c_str()); query.bind(1,view.database_id); storage_detail::bind_link_filters(query,2,view.link_filters);
    // Table rows need titles and properties only; block trees are not decoded.
    std::vector<PageRecord> pages; while (query.row()) pages.push_back(storage_detail::load_summary(database_.get(),query.text(0)));
    auto properties=view; properties.link_filters.clear(); return apply_view(std::move(pages),properties,storage_detail::text_rules());
}
} // namespace nebula
