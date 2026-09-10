#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace footilla::language {

enum class Style : unsigned char {
    Text = 0, Field, Function, Quoted, Comment, Operator, Number, Conditional, Error
};

struct Function {
    std::string_view name;
    std::string_view signature;
    std::string_view description;
};

// Names have no sigils. Signatures include '$'; overloads are separated by newlines.
const std::vector<Function>& Functions();
const std::vector<std::string_view>& Fields();
const Function* FindFunction(std::string_view name);

// Every position and style entry addresses a UTF-8 byte, not a Unicode character.
std::vector<unsigned char> StyleText(std::string_view text);

struct CompletionResult {
    std::size_t start = 0;
    std::size_t end = 0;
    std::vector<std::string> items;
};

// Replaces the complete token [start,end), including its sigil and closing '%'.
// Custom names have no sigils. Unsafe names are ignored. Carets past EOF are clamped.
CompletionResult Complete(std::string_view text, std::size_t caret,
    const std::vector<std::string>& extraFields = {},
    const std::vector<std::string>& extraFunctions = {});

struct CallContext {
    std::string function;
    std::size_t opening = 0;
    std::size_t argument = 0;
};

std::optional<CallContext> FindCall(std::string_view text, std::size_t caret);

} // namespace footilla::language
