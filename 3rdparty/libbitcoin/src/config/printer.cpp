/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/bitcoin/config/printer.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <bitcoin/bitcoin/config/parameter.hpp>
#include <bitcoin/bitcoin/define.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/collection.hpp>
#include <bitcoin/bitcoin/utility/string.hpp>

// We built this because po::options_description.print() sucks.

// TODO: parameterize these localized values.
// Various shared localizable strings.
#define BC_PRINTER_ARGUMENT_TABLE_HEADER "Arguments (positional):"
#define BC_PRINTER_DESCRIPTION_FORMAT "Info: %1%"
#define BC_PRINTER_OPTION_TABLE_HEADER "Options (named):"
#define BC_PRINTER_USAGE_FORMAT "Usage: %1% %2% %3%"
#define BC_PRINTER_VALUE_TEXT "value"

// Not localizable formatters.
#define BC_PRINTER_USAGE_OPTION_MULTIPLE_FORMAT " [--%1% %2%]..."
#define BC_PRINTER_USAGE_OPTION_OPTIONAL_FORMAT " [--%1% %2%]"
#define BC_PRINTER_USAGE_OPTION_REQUIRED_FORMAT " --%1% %2%"
#define BC_PRINTER_USAGE_OPTION_TOGGLE_SHORT_FORMAT " [-%1%]"
#define BC_PRINTER_USAGE_OPTION_TOGGLE_LONG_FORMAT " [--%1%]"

#define BC_PRINTER_USAGE_ARGUMENT_MULTIPLE_FORMAT " [%1%]..."
#define BC_PRINTER_USAGE_ARGUMENT_OPTIONAL_FORMAT " [%1%]"
#define BC_PRINTER_USAGE_ARGUMENT_REQUIRED_FORMAT " %1%"

#define BC_PRINTER_TABLE_OPTION_FORMAT "-%1% [--%2%]"
#define BC_PRINTER_TABLE_OPTION_LONG_FORMAT "--%1%"
#define BC_PRINTER_TABLE_OPTION_SHORT_FORMAT "-%1%"

#define BC_PRINTER_TABLE_ARGUMENT_FORMAT "%1%"

#define BC_PRINTER_SETTING_SECTION_FORMAT "[%1%]\n"
#define BC_PRINTER_SETTING_COMMENT_FORMAT "# %1%\n"
#define BC_PRINTER_SETTING_MULTIPLE_FORMAT "%1% = <%2%>\n%1% = <%2%>\n...\n"
#define BC_PRINTER_SETTING_OPTIONAL_FORMAT "%1% = <%2%>\n"
#define BC_PRINTER_SETTING_REQUIRED_FORMAT "%1% = %2%\n"

namespace po = boost::program_options;
using namespace libbitcoin;
using namespace libbitcoin::config;
using boost::format;

const int printer::max_arguments = 256;

printer::printer(const po::options_description& options,
    const po::positional_options_description& arguments,
    const std::string& application, const std::string& description,
    const std::string& command)
  : options_(options), arguments_(arguments), application_(application),
    description_(description), command_(command)
{
}

printer::printer(const po::options_description& settings,
    const std::string& application, const std::string& description)
  : options_(settings), application_(application), description_(description)
{
}

/* Formatters */

// 100% component tested.
static void enqueue_fragment(std::string& fragment,
    std::vector<std::string>& column)
{
    if (!fragment.empty())
        column.push_back(fragment);
}

// 100% component tested.
std::vector<std::string> printer::columnize(const std::string& paragraph,
    size_t width)
{
    const auto words = split(paragraph, " ", false);

    std::string fragment;
    std::vector<std::string> column;

    for (const auto& word: words)
    {
        if (!fragment.empty() && (word.length() + fragment.length() < width))
        {
            fragment += BC_SENTENCE_DELIMITER + word;
            continue;
        }

        enqueue_fragment(fragment, column);
        fragment = word;
    }

    enqueue_fragment(fragment, column);
    return column;
}

// 100% component tested.
static std::string format_row_name(const parameter& value)
{
    // We hack in upper case for all positional args because of a bug in
    // boost that requires environment variable options to be lower case
    // in order to match any case environment variable, at least on Windows.
    // This is a problem when composing with a command-line argument that
    // wants to be upper case but must match in case with the env var option.

    if (value.get_position() != parameter::not_positional)
        return (format(BC_PRINTER_TABLE_ARGUMENT_FORMAT) %
            boost::to_upper_copy(value.get_long_name())).str();
    else if (value.get_short_name() == parameter::no_short_name)
        return (format(BC_PRINTER_TABLE_OPTION_LONG_FORMAT) %
            value.get_long_name()).str();
    else if (value.get_long_name().empty())
        return (format(BC_PRINTER_TABLE_OPTION_SHORT_FORMAT) %
            value.get_short_name()).str();
    else
        return (format(BC_PRINTER_TABLE_OPTION_FORMAT) %
            value.get_short_name() % value.get_long_name()).str();
}

// 100% component tested.
static bool match_positional(bool positional, const parameter& value)
{
    auto positioned = value.get_position() != parameter::not_positional;
    return positioned == positional;
}

// 100% component tested.
// This formats to 73 char width as: [ 20 | ' ' | 52 | '\n' ]
// GitHub code examples start horizontal scroll after 73 characters.
std::string printer::format_parameters_table(bool positional)
{
    std::stringstream output;
    const auto& parameters = get_parameters();
    format table_format("%-20s %-52s\n");

    for (const auto& parameter: parameters)
    {
        // Skip positional arguments if not positional.
        if (!match_positional(positional, parameter))
            continue;

        // Get the formatted parameter name.
        auto name = format_row_name(parameter);

        // Build a column for the description.
        const auto rows = columnize(parameter.get_description(), 52);

        // If there is no description the command is not output!
        for (const auto& row: rows)
        {
            output << table_format % name % row;

            // The name is only set in the first row.
            name.clear();
        }
    }

    return output.str();
}

// This formats to 73 char width: [ 73 | '\n' ]
// GitHub code examples start horizontal scroll after 73 characters.
std::string printer::format_paragraph(const std::string& paragraph)
{
    std::stringstream output;
    format paragraph_format("%-73s\n");

    const auto lines = columnize(paragraph, 73);

    for (const auto& line: lines)
        output << paragraph_format % line;

    return output.str();
}

static std::string format_setting(const parameter& value,
    const std::string& name)
{
    // A required argument may only be preceeded by required arguments.
    // Requiredness may be in error if the metadata is inconsistent.
    auto required = value.get_required();

    // In terms of formatting we also treat multivalued as not required.
    auto optional = value.get_args_limit() == 1;

    std::string formatter;
    if (required)
        formatter = BC_PRINTER_SETTING_REQUIRED_FORMAT;
    else if (optional)
        formatter = BC_PRINTER_SETTING_OPTIONAL_FORMAT;
    else
        formatter = BC_PRINTER_SETTING_MULTIPLE_FORMAT;

    return (format(formatter) % name % BC_PRINTER_VALUE_TEXT).str();
}

// Requires a single period in each setting (i.e. no unnamed sections).
static void split_setting_name(const parameter& value, std::string& name,
    std::string& section)
{
    const auto tokens = split(value.get_long_name(), ".");
    if (tokens.size() != 2)
    {
        section.clear();
        name.clear();
        return;
    }

    section = tokens[0];
    name = tokens[1];
}

std::string printer::format_settings_table()
{
    std::string name;
    std::string section;
    std::stringstream output;
    std::string preceding_section;

    const auto& parameters = get_parameters();
    for (const auto& parameter: parameters)
    {
        split_setting_name(parameter, name, section);
        if (section.empty())
        {
            BITCOIN_ASSERT_MSG(false, "Invalid config setting metadata.");
            continue;
        }

        if (section != preceding_section)
        {
            output << std::endl;
            if (!section.empty())
            {
                output << format(BC_PRINTER_SETTING_SECTION_FORMAT) % section;
                preceding_section = section;
            }
        }

        output << format(BC_PRINTER_SETTING_COMMENT_FORMAT) %
            parameter.get_description();
        output << format_setting(parameter, name);
    }

    return output.str();
}

std::string printer::format_usage()
{
    // USAGE: bx COMMAND [-hvt] -n VALUE [-m VALUE] [-w VALUE]... REQUIRED
    // [OPTIONAL] [MULTIPLE]...
    auto usage = format(BC_PRINTER_USAGE_FORMAT) % get_application() %
        get_command() % format_usage_parameters();

    return format_paragraph(usage.str());
}

std::string printer::format_description()
{
    // Info: %1%
    auto description = format(BC_PRINTER_DESCRIPTION_FORMAT) %
        get_description();
    return format_paragraph(description.str());
}

// 100% component tested.
std::string printer::format_usage_parameters()
{
    std::string toggle_short_options;
    std::vector<std::string> toggle_long_options;
    std::vector<std::string> required_options;
    std::vector<std::string> optional_options;
    std::vector<std::string> multiple_options;
    std::vector<std::string> required_arguments;
    std::vector<std::string> optional_arguments;
    std::vector<std::string> multiple_arguments;

    const auto& parameters = get_parameters();

    for (const auto& parameter: parameters)
    {
        // A required argument may only be preceeded by required arguments.
        // Requiredness may be in error if the metadata is inconsistent.
        auto required = parameter.get_required();

        // Options are named and args are positional.
        auto option = parameter.get_position() == parameter::not_positional;

        // In terms of formatting we also treat multivalued as not required.
        auto optional = parameter.get_args_limit() == 1;

        // This will capture only options set to zero_tokens().
        auto toggle = parameter.get_args_limit() == 0;

        // A toggle with a short name gets mashed up in group.
        auto is_short = parameter.get_short_name() != parameter::no_short_name;

        const auto& long_name = parameter.get_long_name();

        if (toggle)
        {
            if (is_short)
                toggle_short_options.push_back(parameter.get_short_name());
            else
                toggle_long_options.push_back(long_name);
        }
        else if (option)
        {
            if (required)
                required_options.push_back(long_name);
            else if (optional)
                optional_options.push_back(long_name);
            else
                multiple_options.push_back(long_name);
        }
        else
        {
            // to_upper_copy is a hack for boost bug, see format_row_name.
            if (required)
                required_arguments.push_back(boost::to_upper_copy(long_name));
            else if (optional)
                optional_arguments.push_back(boost::to_upper_copy(long_name));
            else
                multiple_arguments.push_back(boost::to_upper_copy(long_name));
        }
    }

    std::stringstream usage;

    if (!toggle_short_options.empty())
        usage << format(BC_PRINTER_USAGE_OPTION_TOGGLE_SHORT_FORMAT) %
            toggle_short_options;

    for (const auto& required_option: required_options)
        usage << format(BC_PRINTER_USAGE_OPTION_REQUIRED_FORMAT) %
            required_option % BC_PRINTER_VALUE_TEXT;

    for (const auto& toggle_long_option: toggle_long_options)
        usage << format(BC_PRINTER_USAGE_OPTION_TOGGLE_LONG_FORMAT) %
            toggle_long_option;

    for (const auto& optional_option: optional_options)
        usage << format(BC_PRINTER_USAGE_OPTION_OPTIONAL_FORMAT) %
            optional_option % BC_PRINTER_VALUE_TEXT;

    for (const auto& multiple_option: multiple_options)
        usage << format(BC_PRINTER_USAGE_OPTION_MULTIPLE_FORMAT) %
            multiple_option % BC_PRINTER_VALUE_TEXT;

    for (const auto& required_argument: required_arguments)
        usage << format(BC_PRINTER_USAGE_ARGUMENT_REQUIRED_FORMAT) %
            required_argument;

    for (const auto& optional_argument: optional_arguments)
        usage << format(BC_PRINTER_USAGE_ARGUMENT_OPTIONAL_FORMAT) %
            optional_argument;

    for (const auto& multiple_argument: multiple_arguments)
        usage << format(BC_PRINTER_USAGE_ARGUMENT_MULTIPLE_FORMAT) %
            multiple_argument;

    return boost::trim_copy(usage.str());
}

/* Initialization */

// 100% component tested.
static void enqueue_name(int count, std::string& name, argument_list& names)
{
    if (count <= 0)
        return;

    if (count > printer::max_arguments)
        count = -1;

    names.push_back(argument_pair(name, count));
}

// 100% component tested.
// This method just gives us a copy of arguments_metadata private state.
// It would be nice if instead that state was public.
void printer::generate_argument_names()
{
    // Member values
    const auto& arguments = get_arguments();
    auto& argument_names = get_argument_names();

    argument_names.clear();
    const auto max_total_arguments = arguments.max_total_count();

    // Temporary values
    std::string argument_name;
    std::string previous_argument_name;
    int max_previous_argument = 0;

    // We must enumerate all arguments to get the full set of names and counts.
    for (unsigned int position = 0; position < max_total_arguments &&
        max_previous_argument <= max_arguments; ++position)
    {
        argument_name = arguments.name_for_position(position);

        // Initialize the first name as having a zeroth instance.
        if (max_previous_argument == 0)
            previous_argument_name = argument_name;

        // This is a duplicate of the previous name, so increment the count.
        if (argument_name == previous_argument_name)
        {
            ++max_previous_argument;
            continue;
        }

        enqueue_name(max_previous_argument, previous_argument_name,
            argument_names);
        previous_argument_name = argument_name;
        max_previous_argument = 1;
    }

    // Save the previous name (if there is one).
    enqueue_name(max_previous_argument, previous_argument_name,
        argument_names);
}

// 100% component tested.
static bool compare_parameters(const parameter left, const parameter right)
{
    return left.get_format_name() < right.get_format_name();
}

// 100% component tested.
void printer::generate_parameters()
{
    const auto& argument_names = get_argument_names();
    const auto& options = get_options();
    auto& parameters = get_parameters();

    parameters.clear();

    parameter param;
    for (auto option_ptr: options.options())
    {
        param.initialize(*option_ptr, argument_names);

        // Sort non-positonal parameters (i.e. options).
        if (param.get_position() == parameter::not_positional)
            insert_sorted(parameters, param, compare_parameters);
        else
            parameters.push_back(param);
    }
}

// 100% component tested.
void printer::initialize()
{
    generate_argument_names();
    generate_parameters();
}

/* Printers */

void printer::commandline(std::ostream& output)
{
    const auto& option_table = format_parameters_table(false);
    const auto& argument_table = format_parameters_table(true);

    // Don't write a header if a table is empty.
    std::string option_table_header(option_table.empty() ? "" :
        BC_PRINTER_OPTION_TABLE_HEADER "\n");
    std::string argument_table_header(argument_table.empty() ? "" :
        BC_PRINTER_ARGUMENT_TABLE_HEADER "\n");

    output
        << std::endl << format_usage()
        << std::endl << format_description()
        << std::endl << option_table_header
        << std::endl << option_table
        << std::endl << argument_table_header
        << std::endl << argument_table;
}

void printer::settings(std::ostream& output)
{
    const auto& setting_table = format_settings_table();

    if (!description_.empty())
        output << std::endl << description_;

    output << std::endl << setting_table;
}
