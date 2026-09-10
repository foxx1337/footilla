#include "footilla/language.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace footilla::language;

int failures = 0;
std::size_t checks = 0;
std::array<bool, 9> stylesSeen{};

void Check(bool condition, std::string_view expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void CheckStyles(std::initializer_list<std::pair<std::string_view, Style>> parts) {
    std::string text;
    std::vector<unsigned char> expected;
    for (const auto& part : parts) {
        text.append(part.first);
        const auto style = static_cast<unsigned char>(part.second);
        expected.insert(expected.end(), part.first.size(), style);
        stylesSeen[style] = true;
    }
    const auto actual = StyleText(text);
    CHECK(actual.size() == text.size());
    CHECK(actual == expected);
    if (actual != expected) {
        for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
            if (actual[i] != expected[i]) {
                std::cerr << "  style mismatch at UTF-8 byte " << i << ": expected "
                    << static_cast<int>(expected[i]) << ", got "
                    << static_cast<int>(actual[i]) << '\n';
                break;
            }
        }
    }
}

bool Contains(const CompletionResult& result, std::string_view item) {
    return std::find(result.items.begin(), result.items.end(), item) != result.items.end();
}

std::string Folded(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return value;
}

void CheckOrdered(const CompletionResult& result) {
    for (std::size_t i = 1; i < result.items.size(); ++i) {
        CHECK(Folded(result.items[i - 1]) < Folded(result.items[i]));
    }
}

void CheckCall(std::string_view text, std::size_t caret, std::string_view name,
    std::size_t opening, std::size_t argument) {
    const auto call = FindCall(text, caret);
    CHECK(call.has_value());
    if (call) {
        CHECK(call->function == name);
        CHECK(call->opening == opening);
        CHECK(call->argument == argument);
        if (call->function != name || call->opening != opening || call->argument != argument) {
            std::cerr << "  call in [" << text << "] at byte " << caret << ": got "
                << call->function << " opening=" << call->opening
                << " argument=" << call->argument << "; expected " << name
                << " opening=" << opening << " argument=" << argument << '\n';
        }
    }
}

void CheckCallAtEnd(std::string_view text, std::string_view name,
    std::size_t opening, std::size_t argument) {
    CheckCall(text, text.size(), name, opening, argument);
}

void CatalogTests() {
    CHECK(Functions().size() >= 80);
    CHECK(Fields().size() >= 70);
    for (const auto& function : Functions()) {
        CHECK(!function.name.empty());
        CHECK(!function.signature.empty());
        CHECK(!function.description.empty());
        CHECK(function.name.find('$') == std::string_view::npos);
        CHECK(function.signature.front() == '$');
        CHECK(function.signature.find('$' + std::string(function.name) + '(') == 0);
        CHECK(FindFunction(function.name) == &function);
    }
    for (const auto name : {"add", "and", "if", "if2", "if3", "ifgreater", "select",
        "abbr", "ansi", "ascii", "caps2", "cut", "directory_path", "fix_eol", "len2",
        "longest", "padcut_right", "progress2", "replace", "rot13", "stricmp",
        "stripprefix", "swapprefix", "meta", "meta_num", "meta_sep", "meta_test",
        "info", "channels", "year", "day_of_month", "get", "puts", "rgb", "hsl"}) {
        CHECK(FindFunction(name) != nullptr);
    }
    CHECK(FindFunction("IF") == FindFunction("if"));
    CHECK(FindFunction("StRiCmP") == FindFunction("stricmp"));
    CHECK(FindFunction("$if") == nullptr);
    CHECK(FindFunction("my_custom_function") == nullptr);
    CHECK(FindFunction("") == nullptr);
    CHECK(FindFunction("if")->signature.find("$if(cond,then,else)") != std::string_view::npos);
    CHECK(FindFunction("meta")->signature.find("$meta(name,n)") != std::string_view::npos);
    CHECK(FindFunction("rgb")->signature.find("$rgb()") != std::string_view::npos);
    for (const auto field : Fields()) {
        CHECK(!field.empty());
        CHECK(field.find('%') == std::string_view::npos);
    }
    const auto fields = Complete("%", 1);
    for (const auto field : {"%album artist%", "%track artist%", "%track number%",
        "%codec_long%", "%channel_mask%", "%__bitspersample%", "%length_seconds_fp%",
        "%replaygain_track_peak_db%", "%playback_time_remaining_seconds%",
        "%list_total%", "%queue_indexes%", "%_path_raw%", "%_foobar2000_version%"}) {
        CHECK(Contains(fields, field));
    }
}

void StyleTests() {
    CheckStyles({});
    CheckStyles({{u8"café 日本語 ", Style::Text}, {"%album artist%", Style::Field},
        {" ", Style::Text}, {u8"%自作 tag%", Style::Field}});
    CheckStyles({{"$if", Style::Function}, {"(", Style::Operator},
        {"[", Style::Conditional}, {"%album artist%", Style::Field},
        {"]", Style::Conditional}, {",", Style::Operator}, {"$upper", Style::Function},
        {"(", Style::Operator}, {"'a,b''c'", Style::Quoted}, {"),", Style::Operator},
        {"42", Style::Number}, {")", Style::Operator}});
    CheckStyles({{"$custom_function", Style::Function}, {"(", Style::Operator},
        {"%not a builtin%", Style::Field}, {")", Style::Operator}});
    CheckStyles({{"it", Style::Text}, {"''", Style::Quoted}, {"s ", Style::Text},
        {"'[%$]'", Style::Quoted}, {" ", Style::Text}, {"''''", Style::Quoted}});
    CheckStyles({{"'first\r\n//still quoted %title%\n$if()\rlast'", Style::Quoted},
        {"%artist%", Style::Field}});
    CheckStyles({{"  \t", Style::Text}, {"// %title% $if(a,b)", Style::Comment},
        {"\r\n", Style::Text}, {"%title%", Style::Field},
        {"\r", Style::Text}, {"// lone CR", Style::Comment},
        {"\n", Style::Text}, {"\t ", Style::Text}, {"// lone LF", Style::Comment}});
    CheckStyles({{"https://example.test/path // inline text", Style::Text}});
    CheckStyles({{"'x'", Style::Quoted}, {" // not a comment", Style::Text}});
    CheckStyles({{"<", Style::Operator}, {"dimmer", Style::Text}, {">", Style::Operator},
        {" - + = ", Style::Text}, {"12", Style::Number}, {",()", Style::Operator}});
    CheckStyles({{"%tag,with(parens)'and$dollars%", Style::Field}});
    CheckStyles({{"%%", Style::Error}, {" ", Style::Text}, {"%unfinished", Style::Field}});
    CheckStyles({{"%broken", Style::Error}, {"\r\n", Style::Text}, {"%artist%", Style::Field}});
    CheckStyles({{"'unfinished\r\n%artist% $if(", Style::Quoted}});
    CheckStyles({{"$", Style::Function}, {" ", Style::Text}, {"$new", Style::Function}});
    const std::string nulField("%a\0b%", 5);
    CheckStyles({{nulField, Style::Error}});
    const auto afterQuote = StyleText("'x' // not a comment");
    CHECK(afterQuote[0] == static_cast<unsigned char>(Style::Quoted));
    CHECK(afterQuote[4] == static_cast<unsigned char>(Style::Text));
    for (const bool seen : stylesSeen) {
        CHECK(seen);
    }
}

void CompletionTests() {
    const auto allFunctions = Complete("$", 1);
    CHECK(allFunctions.start == 0 && allFunctions.end == 1);
    CHECK(allFunctions.items.size() == Functions().size());
    CHECK(Contains(allFunctions, "$if"));
    CHECK(!Contains(allFunctions, "$if()"));
    CheckOrdered(allFunctions);
    const auto allFields = Complete("%", 1);
    CHECK(allFields.start == 0 && allFields.end == 1);
    CHECK(allFields.items.size() == Fields().size());
    CheckOrdered(allFields);

    auto result = Complete("$IF", 3);
    CHECK(Contains(result, "$if"));
    CHECK(Contains(result, "$ifgreater"));
    CHECK(!Contains(result, "$info"));
    result = Complete("%ALBUM A", 8);
    CHECK(result.items == std::vector<std::string>{"%album artist%"});
    CHECK(result.start == 0 && result.end == 8);
    CHECK(Complete("$not_a_builtin_prefix", 21).items.empty());
    CHECK(Complete("%not_a_builtin_prefix", 21).items.empty());
    CHECK(Complete("plain artist", 12).items.empty());
    CHECK(Complete("", 0).items.empty());
    CHECK(Complete("$if", 0).items.empty());
    CHECK(Complete("%album", 0).items.empty());

    const std::string midpoint = "x %album artist% y";
    result = Complete(midpoint, 5);
    CHECK(Contains(result, "%album%"));
    CHECK(Contains(result, "%album artist%"));
    CHECK(result.start == 2 && result.end == 16);
    std::string replaced = midpoint;
    replaced.replace(result.start, result.end - result.start, "%album%");
    CHECK(replaced == "x %album% y");
    result = Complete("%album%", 6);
    CHECK(result.start == 0 && result.end == 7);
    CHECK(Contains(result, "%album%"));
    CHECK(Complete("%album%", 7).items.empty());
    CHECK(Complete("%album% ", 8).items.empty());
    CHECK(Complete("%artist%%album%", 8).items.empty());
    CHECK(Contains(Complete("%artist%%album%", 9), "%album%"));
    result = Complete("%album", 3);
    CHECK(result.end == 6);
    CHECK(Contains(result, "%album artist%"));
    result = Complete("$IfGreater(%artist%,1,x,y)", 3);
    CHECK(result.start == 0 && result.end == 10);
    CHECK(Contains(result, "$if"));
    replaced = "$IfGreater(%artist%,1,x,y)";
    replaced.replace(result.start, result.end - result.start, "$if");
    CHECK(replaced == "$if(%artist%,1,x,y)");
    CHECK(Complete("$if(", 4).items.empty());
    CHECK(Complete("%al\r\nbum", 8).items.empty());
    CHECK(Complete("%al\nbum", 7).items.empty());
    result = Complete("%al\r\n", 3);
    CHECK(result.end == 3);
    CHECK(Contains(result, "%album%"));
    CHECK(Contains(Complete("$if", std::numeric_limits<std::size_t>::max()), "$if"));

    CHECK(Complete("'$if'", 4).items.empty());
    CHECK(Complete("'%alb%'", 5).items.empty());
    CHECK(Complete("'quoted\r\n$if", 12).items.empty());
    CHECK(Complete("  // $if", 8).items.empty());
    CHECK(Complete("//%album%\r\n", 5).items.empty());
    CHECK(Contains(Complete("''$if", 5), "$if"));
    CHECK(Complete("'a''$if'", 7).items.empty());
    CHECK(Contains(Complete("'x'$if", 6), "$if"));
    CHECK(Contains(Complete("https://example.test/$if", 23), "$if"));
    CHECK(Contains(Complete("x // $if", 8), "$if"));
    CHECK(Contains(Complete("// $bad\r\n$if", 12), "$if"));
    CHECK(Contains(Complete("// $bad\r$if", 11), "$if"));
    CHECK(Contains(Complete("// $bad\n$if", 11), "$if"));
    CHECK(Complete("$f(%something $if%)", 17).items.empty());

    const std::string utf8 = u8"日本 %自作 field%";
    result = Complete(utf8, std::string(u8"日本 %自").size(), {u8"自作 field", u8"自分"});
    CHECK(result.start == std::string(u8"日本 ").size());
    CHECK(result.end == utf8.size());
    CHECK(Contains(result, u8"%自作 field%"));
    CHECK(Contains(result, u8"%自分%"));
    CHECK(Complete(utf8, utf8.size(), {u8"自作 field"}).items.empty());
    result = Complete(u8"$拡張_", std::string(u8"$拡張_").size(), {}, {u8"拡張_func"});
    CHECK(Contains(result, u8"$拡張_func"));

    const std::vector<std::string> customFields = {
        "ALBUM", "Album", "album", "Custom Tag", "custom tag", "Zebra",
        "field,with(parens)", "", "bad\nname", "bad\rname", "bad\tname",
        "%wrapped%", "bad%name", std::string("nul\0name", 8), "bad\x7f"
    };
    result = Complete("%", 1, customFields);
    CheckOrdered(result);
    CHECK(std::count(result.items.begin(), result.items.end(), "%album%") == 1);
    CHECK(Contains(result, "%Custom Tag%"));
    CHECK(Contains(result, "%field,with(parens)%"));
    CHECK(Contains(result, "%Zebra%"));
    CHECK(result.items.size() == Fields().size() + 3);
    result = Complete("%cUsToM t", 9, customFields);
    CHECK(result.items == std::vector<std::string>{"%Custom Tag%"});

    const std::vector<std::string> customFunctions = {
        "IF", "if", "CUSTOM_1", "custom_1", "zebra", "", "$bad", "with space",
        "bad()", "bad,arg", "bad%arg", "bad\nname", "bad\rname", "bad\tname",
        "bad'quote", "bad[condition]", std::string("nul\0name", 8), "bad\x7f"
    };
    result = Complete("$", 1, {}, customFunctions);
    CheckOrdered(result);
    CHECK(result.items.size() == Functions().size() + 2);
    CHECK(Contains(result, "$if"));
    CHECK(Contains(result, "$CUSTOM_1"));
    CHECK(Contains(result, "$zebra"));
    result = Complete("$cUsToM_", 8, {}, customFunctions);
    CHECK(result.items == std::vector<std::string>{"$CUSTOM_1"});
}

void CallTests() {
    CHECK(!FindCall("", 0));
    CHECK(!FindCall("plain text (literal)", 19));
    CHECK(!FindCall("$if", 3));
    CHECK(!FindCall("$if (", 5));
    CHECK(!FindCall("$(", 2));
    CHECK(!FindCall("'$if('", 6));
    CHECK(!FindCall("// $if(", 7));
    CHECK(!FindCall("$if(a,b,c)", 10));
    CheckCall("$if(", 4, "if", 3, 0);
    CheckCall("$if(a,", 6, "if", 3, 1);
    CheckCall("$if(a,b,", 8, "if", 3, 2);
    CheckCall("$if(a,b,c)", 9, "if", 3, 2);
    const std::string nested = "$if(%artist%,$add(1,2),fallback)";
    CheckCall(nested, nested.find('2'), "add", nested.find('(', 4), 1);
    CheckCall(nested, nested.find("fallback"), "if", 3, 2);
    CheckCallAtEnd("$outer($middle($custom(a,", "custom", 22, 1);
    CheckCall("$IF(a,", 6, "IF", 3, 1);
    CheckCall("$plugin_name(a,", 15, "plugin_name", 12, 1);
    CheckCall("$if('a,b)',", 11, "if", 3, 1);
    CheckCallAtEnd("$if('a''b,c',", "if", 3, 1);
    CheckCallAtEnd("$if(%field,with(parens)%,", "if", 3, 1);
    CheckCall("$if(%field,with,commas", 21, "if", 3, 0);
    CheckCall("$if('quoted,\r\n//and )\n',", 24, "if", 3, 1);
    CheckCall("$if('unfinished,)", 17, "if", 3, 0);
    CheckCall("$if(\r\n //,)$fake(\r\nx,", 22, "if", 3, 1);
    CheckCall("$if(\r//,)\rx,", 12, "if", 3, 1);
    CheckCall("$if(\n//,)\nx,", 12, "if", 3, 1);
    CheckCall("$if(http://x,", 13, "if", 3, 1);
    CheckCallAtEnd("$if([%artist%,%album%],", "if", 3, 1);
    CheckCall("$if([a,b", 8, "if", 3, 0);
    CheckCall("$if([a,$add(1,", 14, "add", 11, 1);
    CheckCall("$if([a,$add(1,2),c],", 20, "if", 3, 1);
    CheckCall("$if((a,b),", 10, "if", 3, 1);
    CheckCall("$if((a,b", 8, "if", 3, 0);
    CheckCall("$if(a,", std::numeric_limits<std::size_t>::max(), "if", 3, 1);
    const std::string utf8 = u8"日本 $自作(%曲名%,";
    CheckCall(utf8, utf8.size(), u8"自作", utf8.find('('), 1);
}

// Exercise every caret position and malformed byte sequences without recursion.
void RobustnessTests() {
    const std::string symbols = "$%(),'[]/ \t\r\nabc012<>\x80";
    std::uint32_t random = 0x731a24bdu;
    for (std::size_t sample = 0; sample < 100; ++sample) {
        std::string text;
        for (std::size_t i = 0; i < sample; ++i) {
            random = random * 1664525u + 1013904223u;
            text += symbols[random % symbols.size()];
        }
        const auto styles = StyleText(text);
        CHECK(styles.size() == text.size());
        CHECK(std::all_of(styles.begin(), styles.end(),
            [](unsigned char style) { return style <= static_cast<unsigned char>(Style::Error); }));
        for (std::size_t caret = 0; caret <= text.size(); ++caret) {
            const auto completion = Complete(text, caret);
            if (!completion.items.empty()) {
                CHECK(completion.start < caret);
                CHECK(completion.end >= caret);
                CHECK(completion.end <= text.size());
                CHECK(text[completion.start] == '%' || text[completion.start] == '$');
            }
            const auto call = FindCall(text, caret);
            if (call) {
                CHECK(call->opening < caret);
                CHECK(text[call->opening] == '(');
                CHECK(!call->function.empty());
            }
        }
    }
    std::string deep;
    for (int i = 0; i < 10000; ++i) {
        deep += "$if(";
    }
    CheckCall(deep, deep.size(), "if", deep.size() - 1, 0);
    deep.append(10000, ')');
    CHECK(!FindCall(deep, deep.size()));
    CHECK(StyleText(deep).size() == deep.size());
}

} // namespace

int main() {
    CatalogTests();
    StyleTests();
    CompletionTests();
    CallTests();
    RobustnessTests();
    if (failures != 0) {
        std::cerr << failures << " failures in " << checks << " checks\n";
        return 1;
    }
    std::cout << "Passed " << checks << " language checks\n";
    return 0;
}
