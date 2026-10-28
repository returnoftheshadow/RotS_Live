#include "object_text.h"

#include <utility>

namespace {

/* obj_data's text fields are char* for the many functions that read them, and nothing writes
 * through them, so the shared, const text is handed out as char*. */
char* view_of(const std::optional<std::string>& field)
{
    if (!field) {
        return nullptr;
    }
    return const_cast<char*>(field->c_str());
}

std::optional<std::string> value_of(const char* field)
{
    if (!field) {
        return std::nullopt;
    }
    return std::string(field);
}

std::string empty_if_null(const char* text)
{
    if (!text) {
        return std::string();
    }
    return std::string(text);
}

} // namespace

object_text::object_text(fields text)
    : m_fields(std::move(text))
{
    const std::vector<extra_description>& extras = m_fields.extra_descriptions;
    m_extra_description_nodes.resize(extras.size());
    for (std::size_t index = 0; index < extras.size(); ++index) {
        extra_descr_data& node = m_extra_description_nodes[index];
        node.keyword = const_cast<char*>(extras[index].keyword.c_str());
        node.description = const_cast<char*>(extras[index].description.c_str());
        if (index + 1 < extras.size()) {
            node.next = &m_extra_description_nodes[index + 1];
        } else {
            node.next = nullptr;
        }
    }
}

char* object_text::name() const { return view_of(m_fields.name); }

char* object_text::short_description() const { return view_of(m_fields.short_description); }

char* object_text::description() const { return view_of(m_fields.description); }

char* object_text::action_description() const { return view_of(m_fields.action_description); }

extra_descr_data* object_text::extra_descriptions() const
{
    if (m_extra_description_nodes.empty()) {
        return nullptr;
    }
    return const_cast<extra_descr_data*>(m_extra_description_nodes.data());
}

std::shared_ptr<const object_text> create_object_text(object_text::fields text)
{
    return std::make_shared<const object_text>(std::move(text));
}

object_text::fields object_text_fields_of(const obj_data& object)
{
    object_text::fields text;
    text.name = value_of(object.name);
    text.short_description = value_of(object.short_description);
    text.description = value_of(object.description);
    text.action_description = value_of(object.action_description);
    for (const extra_descr_data* node = object.ex_description; node; node = node->next) {
        object_text::extra_description extra;
        extra.keyword = empty_if_null(node->keyword);
        extra.description = empty_if_null(node->description);
        text.extra_descriptions.push_back(std::move(extra));
    }
    return text;
}

std::shared_ptr<const object_text> shared_text_of(const obj_data& object)
{
    const std::shared_ptr<const object_text>& held = object.shared_text();
    if (held) {
        return held;
    }
    object_text::fields text = object_text_fields_of(object);
    return create_object_text(std::move(text));
}
