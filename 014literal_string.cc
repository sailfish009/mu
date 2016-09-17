//: For convenience, some instructions will take literal arrays of characters
//: (text or strings).
//:
//: Instead of quotes, we'll use [] to delimit strings. That'll reduce the
//: need for escaping since we can support nested brackets. And we can also
//: imagine that 'recipe' might one day itself be defined in mu, doing its own
//: parsing.

:(scenarios load)
:(scenario string_literal)
def main [
  1:address:array:character <- copy [abc def]
]
+parse:   ingredient: {"abc def": "literal-string"}

:(scenario string_literal_with_colons)
def main [
  1:address:array:character <- copy [abc:def/ghi]
]
+parse:   ingredient: {"abc:def/ghi": "literal-string"}

:(before "End Mu Types Initialization")
put(Type_ordinal, "literal-string", 0);

:(before "End next_word Special-cases")
if (in.peek() == '[') {
  string result = slurp_quoted(in);
  skip_whitespace_and_comments_but_not_newline(in);
  return result;
}

:(code)
string slurp_quoted(istream& in) {
  ostringstream out;
  assert(has_data(in));  assert(in.peek() == '[');  out << static_cast<char>(in.get());  // slurp the '['
  if (is_code_string(in, out))
    slurp_quoted_comment_aware(in, out);
  else
    slurp_quoted_comment_oblivious(in, out);
  return out.str();
}

// A string is a code string (ignores comments when scanning for matching
// brackets) if it contains a newline at the start before any non-whitespace.
bool is_code_string(istream& in, ostream& out) {
  while (has_data(in)) {
    char c = in.get();
    if (!isspace(c)) {
      in.putback(c);
      return false;
    }
    out << c;
    if (c == '\n') {
      return true;
    }
  }
  return false;
}

// Read a regular string. Regular strings can only contain other regular
// strings.
void slurp_quoted_comment_oblivious(istream& in, ostream& out) {
  int brace_depth = 1;
  while (has_data(in)) {
    char c = in.get();
    if (c == '\\') {
      slurp_one_past_backslashes(in, out);
      continue;
    }
    out << c;
    if (c == '[') ++brace_depth;
    if (c == ']') --brace_depth;
    if (brace_depth == 0) break;
  }
  if (!has_data(in) && brace_depth > 0) {
    raise << "unbalanced '['\n" << end();
    out.clear();
  }
}

// Read a code string. Code strings can contain either code or regular strings.
void slurp_quoted_comment_aware(istream& in, ostream& out) {
  char c;
  while (in >> c) {
    if (c == '\\') {
      slurp_one_past_backslashes(in, out);
      continue;
    }
    if (c == '#') {
      out << c;
      while (has_data(in) && in.peek() != '\n') out << static_cast<char>(in.get());
      continue;
    }
    if (c == '[') {
      in.putback(c);
      // recurse
      out << slurp_quoted(in);
      continue;
    }
    out << c;
    if (c == ']') return;
  }
  raise << "unbalanced '['\n" << end();
  out.clear();
}

:(after "Parsing reagent(string s)")
if (starts_with(s, "[")) {
  if (*s.rbegin() != ']') return;  // unbalanced bracket; handled elsewhere
  name = s;
  // delete [] delimiters
  name.erase(0, 1);
  strip_last(name);
  type = new type_tree("literal-string", 0);
  return;
}

//: Unlike other reagents, escape newlines in literal strings to make them
//: more friendly to trace().

:(after "string to_string(const reagent& r)")
  if (is_literal_text(r))
    return emit_literal_string(r.name);

:(code)
bool is_literal_text(const reagent& x) {
  return x.type && x.type->name == "literal-string";
}

string emit_literal_string(string name) {
  size_t pos = 0;
  while (pos != string::npos)
    pos = replace(name, "\n", "\\n", pos);
  return "{\""+name+"\": \"literal-string\"}";
}

size_t replace(string& str, const string& from, const string& to, size_t n) {
  size_t result = str.find(from, n);
  if (result != string::npos)
    str.replace(result, from.length(), to);
  return result;
}

void strip_last(string& s) {
  if (!s.empty()) s.erase(SIZE(s)-1);
}

void slurp_one_past_backslashes(istream& in, ostream& out) {
  // When you encounter a backslash, strip it out and pass through any
  // following run of backslashes. If we 'escaped' a single following
  // character, then the character '\' would be:
  //   '\\' escaped once
  //   '\\\\' escaped twice
  //   '\\\\\\\\' escaped thrice (8 backslashes)
  // ..and so on. With our approach it'll be:
  //   '\\' escaped once
  //   '\\\' escaped twice
  //   '\\\\' escaped thrice
  // This only works as long as backslashes aren't also overloaded to create
  // special characters. So Mu doesn't follow C's approach of overloading
  // backslashes both to escape quote characters and also as a notation for
  // unprintable characters like '\n'.
  while (has_data(in)) {
    char c = in.get();
    out << c;
    if (c != '\\') break;
  }
}

:(scenario string_literal_nested)
def main [
  1:address:array:character <- copy [abc [def]]
]
+parse:   ingredient: {"abc [def]": "literal-string"}

:(scenario string_literal_escaped)
def main [
  1:address:array:character <- copy [abc \[def]
]
+parse:   ingredient: {"abc [def": "literal-string"}

:(scenario string_literal_escaped_twice)
def main [
  1:address:array:character <- copy [
abc \\[def]
]
+parse:   ingredient: {"\nabc \[def": "literal-string"}

:(scenario string_literal_and_comment)
def main [
  1:address:array:character <- copy [abc]  # comment
]
+parse: --- defining main
+parse: instruction: copy
+parse:   number of ingredients: 1
+parse:   ingredient: {"abc": "literal-string"}
+parse:   product: {1: ("address" "array" "character")}

:(scenario string_literal_escapes_newlines_in_trace)
def main [
  copy [abc
def]
]
+parse:   ingredient: {"abc\ndef": "literal-string"}

:(scenario string_literal_can_skip_past_comments)
def main [
  copy [
    # ']' inside comment
    bar
  ]
]
+parse:   ingredient: {"\n    # ']' inside comment\n    bar\n  ": "literal-string"}

:(scenario string_literal_empty)
def main [
  copy []
]
+parse:   ingredient: {"": "literal-string"}

:(scenario multiple_unfinished_recipes)
% Hide_errors = true;
def f1 [
def f2 [
+error: unbalanced '['
