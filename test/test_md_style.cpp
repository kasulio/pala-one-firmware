#include <cstring>
#include <string>
#include <vector>

#include "test_framework.h"
#include "pure/md_style.h"

namespace {

struct Run {
  std::string text;
  bool bold;
  bool italic;
};

std::vector<Run> runsOf(const char* line, MdStyleState* io = nullptr) {
  std::vector<Run> out;
  forEachMdStyleRun(line, [&](const char* seg, int len, bool bold, bool italic) {
    out.push_back({std::string(seg, (size_t)len), bold, italic});
  }, io);
  return out;
}

}  // namespace

TEST_CASE("md_style: plain text is one body run") {
  auto runs = runsOf("hello world");
  REQUIRE(runs.size() == 1u);
  CHECK_EQ(runs[0].text, std::string("hello world"));
  CHECK(!runs[0].bold);
  CHECK(!runs[0].italic);
}

TEST_CASE("md_style: paired ** yield bold span") {
  auto runs = runsOf("say **hi** now");
  REQUIRE(runs.size() == 3u);
  CHECK_EQ(runs[0].text, std::string("say "));
  CHECK(!runs[0].bold);
  CHECK(!runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("hi"));
  CHECK(runs[1].bold);
  CHECK(!runs[1].italic);
  CHECK_EQ(runs[2].text, std::string(" now"));
  CHECK(!runs[2].bold);
  CHECK(!runs[2].italic);
}

TEST_CASE("md_style: paired * yield italic span") {
  auto runs = runsOf("say *hi* now");
  REQUIRE(runs.size() == 3u);
  CHECK_EQ(runs[0].text, std::string("say "));
  CHECK(!runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("hi"));
  CHECK(!runs[1].bold);
  CHECK(runs[1].italic);
  CHECK_EQ(runs[2].text, std::string(" now"));
  CHECK(!runs[2].italic);
}

TEST_CASE("md_style: *** yields bold+italic") {
  auto runs = runsOf("***both***");
  REQUIRE(runs.size() == 1u);
  CHECK_EQ(runs[0].text, std::string("both"));
  CHECK(runs[0].bold);
  CHECK(runs[0].italic);
}

TEST_CASE("md_style: nested italic inside bold") {
  auto runs = runsOf("**bold *and* more**");
  REQUIRE(runs.size() == 3u);
  CHECK_EQ(runs[0].text, std::string("bold "));
  CHECK(runs[0].bold);
  CHECK(!runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("and"));
  CHECK(runs[1].bold);
  CHECK(runs[1].italic);
  CHECK_EQ(runs[2].text, std::string(" more"));
  CHECK(runs[2].bold);
  CHECK(!runs[2].italic);
}

TEST_CASE("md_style: unpaired open styles rest of line") {
  auto runs = runsOf("start *rest");
  REQUIRE(runs.size() == 2u);
  CHECK_EQ(runs[0].text, std::string("start "));
  CHECK(!runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("rest"));
  CHECK(runs[1].italic);
}

TEST_CASE("md_style: empty and adjacent toggles emit no empty runs") {
  auto runs = runsOf("****");
  CHECK(runs.empty());

  runs = runsOf("a****b");
  REQUIRE(runs.size() == 2u);
  CHECK_EQ(runs[0].text, std::string("a"));
  CHECK(!runs[0].bold);
  CHECK_EQ(runs[1].text, std::string("b"));
  CHECK(!runs[1].bold);
}

TEST_CASE("md_style: null line is a no-op") {
  auto runs = runsOf(nullptr);
  CHECK(runs.empty());
}

TEST_CASE("md_style: ATX heading strips hashes and reports level") {
  const char* text = nullptr;
  CHECK_EQ(mdAtxHeading("## Einleitung", &text), 2);
  CHECK_EQ(std::string(text), std::string("Einleitung"));

  CHECK_EQ(mdAtxHeading("# Title", &text), 1);
  CHECK_EQ(std::string(text), std::string("Title"));

  CHECK_EQ(mdAtxHeading("###  spaced", &text), 3);
  CHECK_EQ(std::string(text), std::string("spaced"));

  CHECK_EQ(mdAtxHeading("###### deep", &text), 6);
  CHECK_EQ(std::string(text), std::string("deep"));
}

TEST_CASE("md_style: ATX heading rejects non-headings") {
  const char* text = nullptr;
  CHECK_EQ(mdAtxHeading("#NoSpace", &text), 0);
  CHECK_EQ(text, (const char*)"#NoSpace");

  CHECK_EQ(mdAtxHeading(" ## indented", &text), 0);
  CHECK_EQ(mdAtxHeading("plain", &text), 0);
  CHECK_EQ(mdAtxHeading("", &text), 0);
  CHECK_EQ(mdAtxHeading(nullptr, &text), 0);

  // Bare hashes without the required space stay literal (also avoids the
  // paginator's first `##` token measuring as a zero-width empty heading).
  CHECK_EQ(mdAtxHeading("##", &text), 0);
  CHECK_EQ(mdAtxHeading("######", &text), 0);
}

TEST_CASE("md_style: empty ATX heading needs the space") {
  const char* text = nullptr;
  CHECK_EQ(mdAtxHeading("### ", &text), 3);
  CHECK_EQ(std::string(text), std::string(""));
  CHECK_EQ(mdAtxHeading("# ", &text), 1);
  CHECK_EQ(std::string(text), std::string(""));
}

TEST_CASE("md_style: carried italic continues on next visual line") {
  MdStyleState st;
  auto runs = runsOf("start *rest", &st);
  REQUIRE(runs.size() == 2u);
  CHECK(st.italic);
  CHECK(!st.bold);

  // Soft-wrapped continuation: no opening marker, style still open.
  runs = runsOf("of the italic span* done", &st);
  REQUIRE(runs.size() == 2u);
  CHECK_EQ(runs[0].text, std::string("of the italic span"));
  CHECK(runs[0].italic);
  CHECK(!runs[0].bold);
  CHECK_EQ(runs[1].text, std::string(" done"));
  CHECK(!runs[1].italic);
  CHECK(!st.italic);
}

TEST_CASE("md_style: carried bold continues across soft wrap") {
  MdStyleState st;
  mdAdvanceStyle("alpha **bold", &st);
  CHECK(st.bold);
  CHECK(!st.italic);

  auto runs = runsOf("words** tail", &st);
  REQUIRE(runs.size() == 2u);
  CHECK_EQ(runs[0].text, std::string("words"));
  CHECK(runs[0].bold);
  CHECK_EQ(runs[1].text, std::string(" tail"));
  CHECK(!runs[1].bold);
  CHECK(!st.bold);
}

TEST_CASE("md_style: mdAdvanceStyle toggles without emitting") {
  MdStyleState st;
  mdAdvanceStyle("a *b* c **d", &st);
  CHECK(st.bold);
  CHECK(!st.italic);
  mdAdvanceStyle("e**", &st);
  CHECK(!st.bold);
}

TEST_CASE("md_style: adjacent italics do not open bold") {
  auto runs = runsOf("hello, *this is**a test* and now it should be normal again");
  REQUIRE(runs.size() == 4u);
  CHECK_EQ(runs[0].text, std::string("hello, "));
  CHECK(!runs[0].bold);
  CHECK(!runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("this is"));
  CHECK(!runs[1].bold);
  CHECK(runs[1].italic);
  CHECK_EQ(runs[2].text, std::string("a test"));
  CHECK(!runs[2].bold);
  CHECK(runs[2].italic);
  CHECK_EQ(runs[3].text, std::string(" and now it should be normal again"));
  CHECK(!runs[3].bold);
  CHECK(!runs[3].italic);
}

TEST_CASE("md_style: nested bold inside italic still works") {
  auto runs = runsOf("*foo **bar** baz*");
  REQUIRE(runs.size() == 3u);
  CHECK_EQ(runs[0].text, std::string("foo "));
  CHECK(!runs[0].bold);
  CHECK(runs[0].italic);
  CHECK_EQ(runs[1].text, std::string("bar"));
  CHECK(runs[1].bold);
  CHECK(runs[1].italic);
  CHECK_EQ(runs[2].text, std::string(" baz"));
  CHECK(!runs[2].bold);
  CHECK(runs[2].italic);
}

TEST_CASE("md_style: recover style from last newline prefix") {
  MdStyleState st;
  const char* src = "plain\nhello *ital";
  mdRecoverStyle(src, std::strlen(src), &st);
  CHECK(st.italic);
  CHECK(!st.bold);

  mdRecoverStyle("**bold only", std::strlen("**bold only"), &st);
  CHECK(st.bold);
  CHECK(!st.italic);

  mdRecoverStyle("closed *x*\nnext", std::strlen("closed *x*\nnext"), &st);
  CHECK(!st.bold);
  CHECK(!st.italic);

  mdRecoverStyle(nullptr, 10, &st);
  CHECK(!st.italic);
  CHECK(!st.bold);
}
