#pragma once
#include <Print.h>

#include <cstdint>
#include <string>
#include <vector>

struct HtmlLink {
  std::string text;
  std::string url;
};

/**
 * Tolerant, streaming, single-pass HTML-to-text scanner. Real-world HTML is
 * rarely well-formed XML, so this is a small state machine instead of a strict
 * parser: feed() raw chunks straight off the network, cleaned text is written
 * to `out` as it arrives, and the page title plus up to maxLinks links
 * (absolute-ized against the page URL) are collected along the way.
 */
class HtmlToText {
 public:
  HtmlToText(Print& out, const std::string& baseUrl, size_t maxLinks = 64);

  // Feed a chunk of raw HTML. Chunk boundaries may fall anywhere.
  void feed(const uint8_t* data, size_t len);
  // Flush pending state after the last chunk.
  void finish();

  const std::string& getTitle() const { return title; }
  std::vector<HtmlLink>& getLinks() { return links; }

 private:
  enum class State : uint8_t {
    TEXT,        // regular character data
    ENTITY,      // collecting an &entity;
    TAG_OPEN,    // just saw '<'
    TAG_NAME,    // collecting a tag name (open or close)
    IN_TAG,      // between attributes inside a tag
    ATTR_NAME,   // collecting an attribute name
    BEFORE_VAL,  // after '=' waiting for the value
    ATTR_VALUE,  // collecting an attribute value
    MARKUP,      // after "<!", deciding comment vs doctype
    COMMENT,     // inside <!-- ... -->
    DECL,        // skipping to '>' (doctype, <? ... ?>, closing rawtext tag)
    RAWTEXT,     // inside <script>/<style>, scanning for the closing tag
  };

  void step(char c);
  void textChar(char c);
  void emitChar(char c);
  void writeByte(char c);
  void emitNewline(uint8_t want);
  void decodeEntity();
  void emitUtf8(uint32_t cp);
  void finishTag(bool selfClosing);
  void handleOpenTag(const char* name, bool selfClosing);
  void handleCloseTag(const char* name);
  void finishAttrValue();
  std::string resolveUrl(const std::string& href) const;

  Print& out;
  size_t maxLinks;
  std::vector<HtmlLink> links;
  std::string title;

  // Base URL split once so relative hrefs absolutize cheaply
  std::string baseScheme;  // "https"
  std::string baseHost;    // "https://host"
  std::string baseDir;     // "https://host/a/b/" (trailing slash)

  State state = State::TEXT;
  char tagBuf[32];
  uint8_t tagLen = 0;
  bool closingTag = false;
  bool selfCloseSlash = false;
  char attrBuf[16];
  uint8_t attrLen = 0;
  char valueQuote = 0;  // 0 = unquoted
  bool captureHref = false;
  std::string href;      // href of the <a> currently being parsed
  bool hrefValid = false;
  char entityBuf[16];
  uint8_t entityLen = 0;
  uint8_t markupDashes = 0;   // "<!--" progress
  uint8_t commentDashes = 0;  // "-->" progress
  const char* rawTag = nullptr;
  uint8_t rawMatch = 0;
  uint8_t dropDepth = 0;  // >0 inside head/nav/footer/... subtrees
  bool inTitle = false;
  bool titleDone = false;
  bool linkActive = false;
  std::string linkText;
  std::string linkUrl;
  bool pendingSpace = false;
  uint8_t newlines = 2;  // consecutive '\n' already written (2 = at blank line)
  bool wroteAny = false;
};
