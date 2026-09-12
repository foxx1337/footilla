#include "footilla/language.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace footilla::language {
namespace {

unsigned char Fold(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return value >= 'A' && value <= 'Z'
        ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

bool Equal(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(),
            [](char a, char b) { return Fold(a) == Fold(b); });
}

bool Less(std::string_view left, std::string_view right) {
    return std::lexicographical_compare(left.begin(), left.end(),
        right.begin(), right.end(),
        [](char a, char b) { return Fold(a) < Fold(b); });
}

bool HasPrefix(std::string_view name, std::string_view prefix) {
    return name.size() >= prefix.size() && Equal(name.substr(0, prefix.size()), prefix);
}

bool IsNewline(char ch) {
    return ch == '\r' || ch == '\n';
}

bool IsIndent(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f';
}

bool IsControl(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return value < 0x20 || value == 0x7f;
}

bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool IsFunctionByte(char ch) {
    const auto value = Fold(ch);
    return (value >= 'a' && value <= 'z') || IsDigit(ch) ||
        ch == '_' || value >= 0x80;
}

bool SafeName(std::string_view name, bool field) {
    if (name.empty()) {
        return false;
    }
    for (const char ch : name) {
        // Commas and parentheses are safe inside percent-delimited field names.
        if (field ? (IsControl(ch) || ch == '%') : !IsFunctionByte(ch)) {
            return false;
        }
    }
    return true;
}

enum class Kind { Text, Field, Function, Quote, Comment, Symbol, Number };

struct Token {
    Kind kind = Kind::Text;
    Style style = Style::Text;
    std::size_t start = 0;
    std::size_t end = 0;
    bool closed = false;
};

// The same byte scanner drives styling, visual indentation, completion and call-tip context.
// No catalog lookup is involved in lexing: components can add arbitrary names.
class Scanner {
public:
    explicit Scanner(std::string_view text) : text_(text) {}

    bool Next(Token& token) {
        if (position_ == text_.size()) {
            return false;
        }
        token = {};
        token.start = position_;
        std::size_t end = position_ + 1;
        const char ch = text_[position_];

        if (lineIndent_ && ch == '/' && end < text_.size() && text_[end] == '/') {
            token.kind = Kind::Comment;
            token.style = Style::Comment;
            while (end < text_.size() && !IsNewline(text_[end])) {
                ++end;
            }
        } else if (ch == '\'') {
            token.kind = Kind::Quote;
            token.style = Style::Quoted;
            if (end < text_.size() && text_[end] == '\'') {
                ++end;
                token.closed = true;
            } else {
                while (end < text_.size()) {
                    if (text_[end++] == '\'') {
                        if (end < text_.size() && text_[end] == '\'') {
                            ++end;
                        } else {
                            token.closed = true;
                            break;
                        }
                    }
                }
            }
        } else if (ch == '%') {
            token.kind = Kind::Field;
            token.style = Style::Field;
            while (end < text_.size() && text_[end] != '%' && !IsNewline(text_[end])) {
                if (IsControl(text_[end])) {
                    token.style = Style::Error;
                }
                ++end;
            }
            if (end < text_.size() && text_[end] == '%') {
                if (end == position_ + 1) {
                    token.style = Style::Error;
                }
                ++end;
                token.closed = true;
            } else if (end < text_.size()) {
                // A field cannot continue on the next line; EOF alone is normal typing.
                token.style = Style::Error;
            }
        } else if (ch == '$') {
            token.kind = Kind::Function;
            token.style = Style::Function;
            while (end < text_.size() && IsFunctionByte(text_[end])) {
                ++end;
            }
        } else if (ch == '[' || ch == ']') {
            token.kind = Kind::Symbol;
            token.style = Style::Conditional;
        } else if (ch == '(' || ch == ')' || ch == ',' || ch == '<' || ch == '>') {
            token.kind = Kind::Symbol;
            token.style = Style::Operator;
        } else if (IsDigit(ch)) {
            token.kind = Kind::Number;
            token.style = Style::Number;
            while (end < text_.size() && IsDigit(text_[end])) {
                ++end;
            }
        }

        for (; position_ < end; ++position_) {
            if (IsNewline(text_[position_])) {
                lineIndent_ = true;
            } else if (!IsIndent(text_[position_])) {
                lineIndent_ = false;
            }
        }
        token.end = end;
        return true;
    }

private:
    std::string_view text_;
    std::size_t position_ = 0;
    bool lineIndent_ = true;
};

} // namespace

// Names, overloads and scope follow the Hydrogenaudio title-format reference:
// https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Title_Formatting_Reference
// Descriptions below are concise, original summaries, not copied reference prose.
const std::vector<Function>& Functions() {
    static const std::vector<Function> functions = {
        {"abbr", "$abbr(str)\n$abbr(str,len)", "Abbreviate words, optionally only above a length limit."},
        {"add", "$add(a,b,...)", "Sum integer arguments."},
        {"and", "$and(expr,...)", "True when every argument has a true title-format truth value."},
        {"ansi", "$ansi(str)", "Round-trip text through the system legacy code page."},
        {"ascii", "$ascii(str)", "Reduce text to an ASCII representation."},
        {"blend", "$blend(color1,color2,part,total)", "Blend two RGB colors; legacy/Columns UI color formatting."},
        {"caps", "$caps(str)", "Capitalize words and lowercase their remaining letters."},
        {"caps2", "$caps2(str)", "Capitalize words without lowercasing other letters."},
        {"channels", "$channels()", "Describe the channel count, such as mono or stereo."},
        {"char", "$char(nbr)", "Create a character from a Unicode code point."},
        {"crc32", "$crc32(str)", "Calculate a numeric CRC32 for text."},
        {"crlf", "$crlf()", "Produce a carriage return and line feed in the output."},
        {"cut", "$cut(str,len)", "Keep a string's first characters; equivalent to left."},
        {"date", "$date(time)", "Extract the YYYY-MM-DD portion of a timestamp."},
        {"day_of_month", "$day_of_month(time)", "Extract a timestamp's two-digit day."},
        {"directory", "$directory(path)\n$directory(path,n)", "Get a directory name, optionally an ancestor n levels up."},
        {"directory_path", "$directory_path(path)", "Remove the filename from a path."},
        {"div", "$div(a,b,...)", "Divide integers successively; a zero divisor leaves the dividend."},
        {"ext", "$ext(path)", "Extract a filename's extension."},
        {"filename", "$filename(path)", "Extract a filename without its directory or extension."},
        {"fix_eol", "$fix_eol(str)\n$fix_eol(str,indicator)", "Replace the first line break and following text with an indicator."},
        {"get", "$get(name)", "Read a script variable; undefined variables produce empty text."},
        {"greater", "$greater(a,b)", "Test whether integer a exceeds integer b."},
        {"hex", "$hex(int,len)", "Format an integer as zero-padded hexadecimal."},
        {"hsl", "$hsl()\n$hsl(h,s,l)\n$hsl(h1,s1,l1,h2,s2,l2)", "Reset or set HSL text colors; legacy/Columns UI only."},
        {"if", "$if(cond,then)\n$if(cond,then,else)", "Choose text using a title-format truth value, not string nonemptiness."},
        {"if2", "$if2(expr,else)", "Use expr when true, otherwise evaluate the fallback."},
        {"if3", "$if3(a1,a2,...,aN,else)", "Use the first true expression, or the final fallback."},
        {"ifequal", "$ifequal(int1,int2,then,else)", "Choose a branch by integer equality."},
        {"ifgreater", "$ifgreater(int1,int2,then,else)", "Choose a branch by integer greater-than comparison."},
        {"iflonger", "$iflonger(str,n,then,else)", "Choose a branch by whether text exceeds n characters."},
        {"info", "$info(name)", "Read technical information rather than a metadata tag."},
        {"insert", "$insert(str,insert,n)", "Insert text after n characters."},
        {"left", "$left(str,len)", "Keep the first len characters."},
        {"len", "$len(str)", "Count characters in text."},
        {"len2", "$len2(str)", "Count characters with double-width characters counted twice."},
        {"longer", "$longer(str1,str2)", "Test whether the first string has more characters."},
        {"longest", "$longest(arg,...)", "Choose the longest argument."},
        {"lower", "$lower(str)", "Convert letters to lowercase."},
        {"max", "$max(a,b,...)", "Choose the largest integer argument."},
        {"meta", "$meta(name)\n$meta(name,n)", "Read all values of a tag, or its zero-based nth value."},
        {"meta_num", "$meta_num(name)", "Count the values of a metadata tag."},
        {"meta_sep", "$meta_sep(name,sep)\n$meta_sep(name,sep,lastsep)", "Join tag values with configurable separators."},
        {"meta_test", "$meta_test(name,...)", "Test whether every named metadata tag is present."},
        {"min", "$min(a,b,...)", "Choose the smallest integer argument."},
        {"mod", "$mod(a,b,...)", "Take successive integer remainders, preserving the dividend's sign."},
        {"month", "$month(time)", "Extract a timestamp's two-digit month."},
        {"mul", "$mul(a,b,...)", "Multiply integer arguments."},
        {"muldiv", "$muldiv(a,b,c)", "Multiply a by b, divide by c, and round to the nearest integer."},
        {"not", "$not(expr)", "Invert an expression's title-format truth value."},
        {"num", "$num(nbr,len)", "Format an integer in decimal with leading zero padding."},
        {"or", "$or(expr,...)", "True when any argument has a true title-format truth value."},
        {"pad", "$pad(str,len)\n$pad(str,len,char)", "Append spaces or a chosen character to reach a minimum width."},
        {"pad_right", "$pad_right(str,len)\n$pad_right(str,len,char)", "Prepend padding to right-align text."},
        {"padcut", "$padcut(str,len)\n$padcut(str,len,char)", "Truncate or append padding to produce a fixed width."},
        {"padcut_right", "$padcut_right(str,len)\n$padcut_right(str,len,char)", "Truncate or prepend padding to produce a fixed width."},
        {"progress", "$progress(pos,range,len,char1,char2)", "Build a progress bar with a moving marker."},
        {"progress2", "$progress2(pos,range,len,char1,char2)", "Build a progress bar with a filled portion."},
        {"put", "$put(name,value)", "Store a script variable and also output its value."},
        {"puts", "$puts(name,value)", "Store a script variable without output."},
        {"rand", "$rand()", "Generate an unsigned 32-bit random value in sorting contexts."},
        {"repeat", "$repeat(expr,count)", "Repeat the value of an expression evaluated once."},
        {"replace", "$replace(str,search,replace,...)", "Replace matches using one or more search/replacement pairs."},
        {"rgb", "$rgb()\n$rgb(r,g,b)\n$rgb(r1,g1,b1,r2,g2,b2)", "Reset or set RGB text colors; legacy/Columns UI only."},
        {"right", "$right(str,len)", "Keep the final len characters."},
        {"roman", "$roman(int)", "Render an integer using Roman numerals."},
        {"rot13", "$rot13(str)", "Rotate Latin letters by thirteen places."},
        {"select", "$select(n,a1,...,aN)", "Evaluate the nth choice using a one-based index."},
        {"shortest", "$shortest(str,...,strN)", "Choose the first shortest argument."},
        {"strchr", "$strchr(str,char)", "Find the first character occurrence; positions start at one."},
        {"strcmp", "$strcmp(str1,str2)", "Test text equality with case sensitivity."},
        {"stricmp", "$stricmp(str1,str2)", "Test text equality without case sensitivity."},
        {"stripprefix", "$stripprefix(str)\n$stripprefix(str,prefix1,prefix2,...)", "Remove a leading prefix; defaults to A and The."},
        {"strrchr", "$strrchr(str,char)", "Find the final character occurrence; positions start at one."},
        {"strstr", "$strstr(str1,str2)", "Find a case-sensitive substring; positions start at one."},
        {"sub", "$sub(a,b,...)", "Subtract successive integer arguments."},
        {"substr", "$substr(str,from,to)", "Extract an inclusive, one-based character range."},
        {"swapprefix", "$swapprefix(str)\n$swapprefix(str,prefix1,prefix2,...)", "Move a leading prefix to the end; defaults to A and The."},
        {"tab", "$tab()\n$tab(count)", "Output one tab or a specified number of tabs."},
        {"time", "$time(time)", "Extract the time-of-day portion of a timestamp."},
        {"transition", "$transition(string,color1,color2)", "Apply a color gradient to text; legacy/Columns UI only."},
        {"trim", "$trim(str)", "Remove spaces at both ends of text."},
        {"upper", "$upper(str)", "Convert letters to uppercase."},
        {"xor", "$xor(expr,...)", "True when an odd number of arguments have true truth values."},
        {"year", "$year(time)", "Extract a timestamp's four-digit year."}
    };
    return functions;
}

const std::vector<std::string_view>& Fields() {
    static const std::vector<std::string_view> fields = {
        // Remapped fields and commonly used, directly mapped metadata tags.
        "album", "album artist", "artist", "comment", "composer", "conductor",
        "date", "disc", "discnumber", "genre", "performer", "publisher", "title",
        "totaldiscs", "totaltracks", "track artist", "track number", "tracknumber",
        "url", "venue",
        // Technical information, ReplayGain and file identity.
        "bitrate", "channel_mask", "channels", "codec", "codec_long", "codec_profile",
        "directoryname", "filename", "filename_ext", "filesize", "filesize_natural",
        "last_modified", "length", "length_ex", "length_samples", "length_seconds",
        "length_seconds_fp", "path", "replaygain_album_gain", "replaygain_album_peak",
        "replaygain_album_peak_db", "replaygain_track_gain", "replaygain_track_peak",
        "replaygain_track_peak_db", "samplerate", "subsong", "_foobar2000_version",
        "_path_raw",
        // $info(name) aliases; actual availability depends on the decoder.
        "__bitrate", "__bitspersample", "__bitspersample_extra", "__channels",
        "__channel_mode", "__codec", "__codec_profile", "__cue_embedded",
        "__decoded_bitspersample", "__enc_delay", "__enc_padding", "__encoding",
        "__flags", "__md5", "__mp3_accurate_length", "__mp3_stereo_mode",
        "__samplerate", "__tagtype", "__tool", "__version",
        "__waveformatextensible_channel_mask",
        // Playback and playlist display contexts.
        "ispaused", "isplaying", "list_index", "list_total", "playback_time",
        "playback_time_remaining", "playback_time_remaining_seconds",
        "playback_time_seconds", "queue_index", "queue_indexes", "queue_total"
    };
    return fields;
}

const Function* FindFunction(std::string_view name) {
    for (const auto& function : Functions()) {
        if (Equal(function.name, name)) {
            return &function;
        }
    }
    return nullptr;
}

std::vector<unsigned char> StyleText(std::string_view text) {
    std::vector<unsigned char> styles(text.size(), static_cast<unsigned char>(Style::Text));
    Scanner scanner(text);
    Token token;
    while (scanner.Next(token)) {
        std::fill(styles.begin() + static_cast<std::ptrdiff_t>(token.start),
            styles.begin() + static_cast<std::ptrdiff_t>(token.end),
            static_cast<unsigned char>(token.style));
    }
    return styles;
}

std::vector<int> VisualIndentLevels(std::string_view text) {
    struct Frame {
        char closing;
        bool inFunction;
    };
    std::vector<Frame> stack;
    std::vector<int> levels{0};
    bool leadingClosers = true;
    bool followsFunction = false;
    const auto open = [&](char closing, bool inFunction) {
        if (stack.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::length_error("Visual indentation nesting exceeds int range");
        }
        stack.push_back({closing, inFunction});
    };
    Scanner scanner(text);
    Token token;
    while (scanner.Next(token)) {
        for (std::size_t i = token.start; i < token.end; ++i) {
            const char ch = text[i];
            if (IsNewline(ch)) {
                if (ch != '\n' || i == 0 || text[i - 1] != '\r') {
                    levels.push_back(static_cast<int>(stack.size()));
                    leadingClosers = true;
                }
                continue;
            }
            if (token.kind == Kind::Symbol) {
                const bool inFunction = !stack.empty() && stack.back().inFunction;
                if (ch == '[') {
                    open(']', inFunction);
                } else if (ch == '(' && (followsFunction || inFunction)) {
                    // Balance generic argument grouping without treating literal
                    // parentheses outside an actual call as function scope.
                    open(')', true);
                } else if (ch == ')' || ch == ']') {
                    if (!stack.empty() && stack.back().closing == ch) {
                        stack.pop_back();
                    }
                    if (leadingClosers) {
                        levels.back() = static_cast<int>(stack.size());
                    }
                    continue;
                }
            }
            // Even spaces in a continuing quote belong to its literal prefix.
            if (token.kind != Kind::Text || (ch != ' ' && ch != '\t')) {
                leadingClosers = false;
            }
        }
        followsFunction = token.kind == Kind::Function && token.end - token.start > 1;
    }
    return levels;
}

CompletionResult Complete(std::string_view text, std::size_t caret,
    const std::vector<std::string>& extraFields,
    const std::vector<std::string>& extraFunctions) {
    caret = std::min(caret, text.size());
    Scanner scanner(text);
    Token token;
    while (scanner.Next(token) && token.start < caret) {
        const bool field = token.kind == Kind::Field;
        const bool function = token.kind == Kind::Function;
        if ((!field && !function) || caret > token.end ||
            (field && token.closed && caret == token.end)) {
            continue;
        }

        const auto prefix = text.substr(token.start + 1, caret - token.start - 1);
        if (!prefix.empty() && !SafeName(prefix, field)) {
            return {};
        }
        CompletionResult result;
        result.start = token.start;
        result.end = token.end;
        const auto add = [&](std::string_view name) {
            if (SafeName(name, field) && HasPrefix(name, prefix)) {
                std::string item(1, field ? '%' : '$');
                item.append(name);
                if (field) {
                    item += '%';
                }
                result.items.push_back(std::move(item));
            }
        };
        if (field) {
            for (const auto name : Fields()) {
                add(name);
            }
            for (const auto& name : extraFields) {
                add(name);
            }
        } else {
            for (const auto& entry : Functions()) {
                add(entry.name);
            }
            for (const auto& name : extraFunctions) {
                add(name);
            }
        }
        // Stable ordering keeps canonical catalog spelling ahead of custom duplicates.
        std::stable_sort(result.items.begin(), result.items.end(),
            [](const std::string& a, const std::string& b) { return Less(a, b); });
        result.items.erase(std::unique(result.items.begin(), result.items.end(),
            [](const std::string& a, const std::string& b) { return Equal(a, b); }),
            result.items.end());
        return result;
    }
    return {};
}

std::optional<CallContext> FindCall(std::string_view text, std::size_t caret) {
    struct Frame {
        std::string_view function;
        std::size_t opening;
        std::size_t argument;
        std::size_t conditionalDepth;
    };
    caret = std::min(caret, text.size());
    std::vector<Frame> stack;
    std::size_t conditionalDepth = 0;
    std::string_view function;
    std::size_t functionEnd = 0;
    Scanner scanner(text);
    Token token;
    while (scanner.Next(token) && token.end <= caret) {
        if (token.kind == Kind::Function) {
            function = text.substr(token.start + 1, token.end - token.start - 1);
            functionEnd = token.end;
        } else if (token.kind == Kind::Symbol) {
            switch (text[token.start]) {
            case '(':
                stack.push_back({functionEnd == token.start ? function : std::string_view{},
                    token.start, 0, conditionalDepth});
                break;
            case ')':
                if (!stack.empty()) {
                    conditionalDepth = stack.back().conditionalDepth;
                    stack.pop_back();
                }
                break;
            case '[':
                ++conditionalDepth;
                break;
            case ']':
                if (conditionalDepth != 0) {
                    --conditionalDepth;
                }
                break;
            case ',':
                if (!stack.empty() && stack.back().conditionalDepth == conditionalDepth) {
                    ++stack.back().argument;
                }
                break;
            default:
                break;
            }
        }
    }
    for (auto frame = stack.rbegin(); frame != stack.rend(); ++frame) {
        if (!frame->function.empty()) {
            return CallContext{std::string(frame->function), frame->opening, frame->argument};
        }
    }
    return std::nullopt;
}

} // namespace footilla::language
