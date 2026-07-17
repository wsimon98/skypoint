#include "HtmlToText.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t LINK_TEXT_CAP = 96;
constexpr size_t TITLE_CAP = 96;
constexpr size_t HREF_CAP = 512;

// Tags whose whole subtree is boilerplate, not article text. script/style are
// handled separately via RAWTEXT (their contents aren't markup).
bool isDropTag(const char* t) {
  static constexpr const char* const DROP[] = {"head",  "template", "noscript", "svg",
                                               "iframe", "form",     "nav",      "footer", "aside"};
  for (const char* d : DROP) {
    if (strcmp(t, d) == 0) return true;
  }
  return false;
}

bool isRawTextTag(const char* t) { return strcmp(t, "script") == 0 || strcmp(t, "style") == 0; }

bool isBlockTag(const char* t) {
  static constexpr const char* const BLOCK[] = {"p",  "div",   "li",    "tr",     "blockquote", "section", "article",
                                                "ul", "ol",    "table", "header", "main",       "figure",  "pre",
                                                "hr", "figcaption"};
  for (const char* b : BLOCK) {
    if (strcmp(t, b) == 0) return true;
  }
  return false;
}

bool isHeadingTag(const char* t) { return t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0'; }

bool isWs(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }

}  // namespace

HtmlToText::HtmlToText(Print& out, const std::string& baseUrl, const size_t maxLinks) : out(out), maxLinks(maxLinks) {
  // links is bounded by maxLinks; reserving up front avoids grow-copy-free
  // cycles on the heap while the page streams through (~3 KB for 64 entries).
  links.reserve(maxLinks);

  // Split the base URL once: scheme, "scheme://host", and the directory of the
  // page (with trailing slash) for relative href resolution.
  const size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd != std::string::npos) {
    baseScheme = baseUrl.substr(0, schemeEnd);
    const size_t hostStart = schemeEnd + 3;
    const size_t pathStart = baseUrl.find('/', hostStart);
    if (pathStart == std::string::npos) {
      baseHost = baseUrl;
      baseDir = baseUrl + "/";
    } else {
      baseHost = baseUrl.substr(0, pathStart);
      const size_t lastSlash = baseUrl.rfind('/');
      baseDir = baseUrl.substr(0, lastSlash + 1);
    }
  }
}

void HtmlToText::feed(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    step(static_cast<char>(data[i]));
  }
}

void HtmlToText::finish() {
  if (state == State::ENTITY) {
    // Unterminated entity at EOF: emit it literally.
    emitChar('&');
    for (uint8_t i = 0; i < entityLen; i++) emitChar(entityBuf[i]);
  }
  if (linkActive) handleCloseTag("a");
  if (wroteAny && newlines == 0) writeByte('\n');
}

void HtmlToText::step(const char c) {
  switch (state) {
    case State::TEXT:
      if (c == '<') {
        state = State::TAG_OPEN;
      } else if (c == '&') {
        state = State::ENTITY;
        entityLen = 0;
      } else {
        textChar(c);
      }
      break;

    case State::ENTITY:
      if (c == ';') {
        decodeEntity();
        state = State::TEXT;
      } else if (entityLen >= sizeof(entityBuf) - 1 || c == '<' || isWs(c)) {
        // Not a real entity ("AT&T", stray '&'): emit literally, reprocess c.
        emitChar('&');
        for (uint8_t i = 0; i < entityLen; i++) emitChar(entityBuf[i]);
        state = State::TEXT;
        step(c);
      } else {
        entityBuf[entityLen++] = c;
      }
      break;

    case State::TAG_OPEN:
      tagLen = 0;
      selfCloseSlash = false;
      if (c == '!') {
        state = State::MARKUP;
        markupDashes = 0;
      } else if (c == '?') {
        state = State::DECL;
      } else if (c == '/') {
        closingTag = true;
        state = State::TAG_NAME;
      } else if (isalpha(static_cast<unsigned char>(c))) {
        closingTag = false;
        tagBuf[tagLen++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        state = State::TAG_NAME;
      } else {
        // Lone '<' in text.
        emitChar('<');
        state = State::TEXT;
        step(c);
      }
      break;

    case State::TAG_NAME:
      if (c == '>') {
        tagBuf[tagLen] = '\0';
        finishTag(selfCloseSlash);
      } else if (c == '/') {
        selfCloseSlash = true;
      } else if (isWs(c)) {
        tagBuf[tagLen] = '\0';
        state = State::IN_TAG;
      } else if (tagLen < sizeof(tagBuf) - 1) {
        tagBuf[tagLen++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      }
      break;

    case State::IN_TAG:
      if (c == '>') {
        finishTag(selfCloseSlash);
      } else if (c == '/') {
        selfCloseSlash = true;
      } else if (!isWs(c)) {
        selfCloseSlash = false;
        attrLen = 0;
        attrBuf[attrLen++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        state = State::ATTR_NAME;
      }
      break;

    case State::ATTR_NAME:
      if (c == '=') {
        attrBuf[attrLen] = '\0';
        captureHref = !closingTag && strcmp(tagBuf, "a") == 0 && strcmp(attrBuf, "href") == 0;
        href.clear();
        state = State::BEFORE_VAL;
      } else if (c == '>') {
        finishTag(selfCloseSlash);
      } else if (isWs(c)) {
        state = State::IN_TAG;
      } else if (c == '/') {
        selfCloseSlash = true;
      } else if (attrLen < sizeof(attrBuf) - 1) {
        attrBuf[attrLen++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      }
      break;

    case State::BEFORE_VAL:
      if (c == '"' || c == '\'') {
        valueQuote = c;
        state = State::ATTR_VALUE;
      } else if (c == '>') {
        finishTag(selfCloseSlash);
      } else if (!isWs(c)) {
        valueQuote = 0;
        state = State::ATTR_VALUE;
        if (captureHref && href.size() < HREF_CAP) href += c;
      }
      break;

    case State::ATTR_VALUE:
      if (valueQuote != 0 ? (c == valueQuote) : (isWs(c) || c == '>')) {
        finishAttrValue();
        if (valueQuote == 0 && c == '>') {
          finishTag(selfCloseSlash);
        } else {
          state = State::IN_TAG;
        }
      } else if (captureHref && href.size() < HREF_CAP) {
        href += c;
      }
      break;

    case State::MARKUP:
      if (c == '-' && markupDashes < 2) {
        if (++markupDashes == 2) {
          state = State::COMMENT;
          commentDashes = 0;
        }
      } else if (c == '>') {
        state = State::TEXT;
      } else {
        state = State::DECL;
      }
      break;

    case State::COMMENT:
      if (c == '-') {
        if (commentDashes < 2) commentDashes++;
      } else if (c == '>' && commentDashes >= 2) {
        state = State::TEXT;
      } else {
        commentDashes = 0;
      }
      break;

    case State::DECL:
      if (c == '>') state = State::TEXT;
      break;

    case State::RAWTEXT: {
      // Scan for "</script" / "</style" (case-insensitive), then skip to '>'.
      const char expected = rawMatch < 2 ? "</"[rawMatch] : rawTag[rawMatch - 2];
      if (expected == '\0') {
        // Full needle matched; wait for the closing '>'.
        if (c == '>') {
          state = State::TEXT;
        } else if (!isWs(c)) {
          rawMatch = c == '<' ? 1 : 0;
        }
      } else if (static_cast<char>(tolower(static_cast<unsigned char>(c))) == expected) {
        rawMatch++;
      } else {
        rawMatch = c == '<' ? 1 : 0;
      }
      break;
    }
  }
}

void HtmlToText::finishAttrValue() {
  if (captureHref) {
    hrefValid = !href.empty();
    captureHref = false;
  }
}

void HtmlToText::finishTag(const bool selfClosing) {
  tagBuf[tagLen] = '\0';
  state = State::TEXT;
  if (closingTag) {
    handleCloseTag(tagBuf);
  } else {
    handleOpenTag(tagBuf, selfClosing);
  }
  closingTag = false;
  href.clear();
  hrefValid = false;
  captureHref = false;
}

void HtmlToText::handleOpenTag(const char* name, const bool selfClosing) {
  if (dropDepth > 0) {
    // Inside a dropped subtree: only track nesting of further drop-tags.
    if (isDropTag(name) && !selfClosing) dropDepth++;
    return;
  }

  if (isRawTextTag(name) && !selfClosing) {
    rawTag = name[1] == 'c' ? "script" : "style";
    rawMatch = 0;
    state = State::RAWTEXT;
    return;
  }

  if (isDropTag(name) && !selfClosing) {
    dropDepth = 1;
    return;
  }

  if (strcmp(name, "title") == 0 && !titleDone) {
    inTitle = true;
    return;
  }

  if (strcmp(name, "a") == 0) {
    linkActive = true;
    linkText.clear();
    linkUrl = hrefValid ? resolveUrl(href) : "";
    return;
  }

  if (isHeadingTag(name)) {
    emitNewline(2);
  } else if (strcmp(name, "br") == 0 || isBlockTag(name)) {
    emitNewline(1);
  }
}

void HtmlToText::handleCloseTag(const char* name) {
  if (dropDepth > 0) {
    if (isDropTag(name)) dropDepth--;
    return;
  }

  if (strcmp(name, "title") == 0 && inTitle) {
    inTitle = false;
    titleDone = true;
    // Trim trailing whitespace picked up inside <title>.
    while (!title.empty() && isWs(title.back())) title.pop_back();
    return;
  }

  if (strcmp(name, "a") == 0 && linkActive) {
    linkActive = false;
    while (!linkText.empty() && isWs(linkText.back())) linkText.pop_back();
    if (!linkUrl.empty() && !linkText.empty() && links.size() < maxLinks) {
      links.push_back(HtmlLink{linkText, linkUrl});
    }
    linkText.clear();
    linkUrl.clear();
    return;
  }

  if (isHeadingTag(name)) {
    emitNewline(2);
  } else if (isBlockTag(name)) {
    emitNewline(1);
  }
}

void HtmlToText::textChar(const char c) {
  if (dropDepth > 0) return;

  if (inTitle) {
    if (isWs(c)) {
      if (!title.empty() && title.back() != ' ' && title.size() < TITLE_CAP) title += ' ';
    } else if (title.size() < TITLE_CAP) {
      title += c;
    }
    return;
  }

  emitChar(c);
}

void HtmlToText::emitChar(const char c) {
  if (isWs(c)) {
    if (wroteAny && newlines == 0) pendingSpace = true;
    return;
  }
  if (pendingSpace) {
    pendingSpace = false;
    writeByte(' ');
    if (linkActive && !linkText.empty() && linkText.size() < LINK_TEXT_CAP) linkText += ' ';
  }
  writeByte(c);
  if (linkActive && linkText.size() < LINK_TEXT_CAP) linkText += c;
}

void HtmlToText::writeByte(const char c) {
  out.write(static_cast<uint8_t>(c));
  newlines = c == '\n' ? newlines + 1 : 0;
  if (c != '\n') wroteAny = true;
}

void HtmlToText::emitNewline(const uint8_t want) {
  if (!wroteAny) return;
  pendingSpace = false;
  while (newlines < want) writeByte('\n');
}

void HtmlToText::decodeEntity() {
  entityBuf[entityLen] = '\0';
  if (entityBuf[0] == '#') {
    const uint32_t cp = entityBuf[1] == 'x' || entityBuf[1] == 'X'
                            ? strtoul(entityBuf + 2, nullptr, 16)
                            : strtoul(entityBuf + 1, nullptr, 10);
    if (cp > 0 && cp < 0x110000) emitUtf8(cp);
    return;
  }
  if (strcmp(entityBuf, "amp") == 0) {
    emitChar('&');
  } else if (strcmp(entityBuf, "lt") == 0) {
    emitChar('<');
  } else if (strcmp(entityBuf, "gt") == 0) {
    emitChar('>');
  } else if (strcmp(entityBuf, "quot") == 0) {
    emitChar('"');
  } else if (strcmp(entityBuf, "apos") == 0) {
    emitChar('\'');
  } else if (strcmp(entityBuf, "nbsp") == 0) {
    emitChar(' ');
  } else if (strcmp(entityBuf, "mdash") == 0 || strcmp(entityBuf, "ndash") == 0) {
    emitChar('-');
  } else if (strcmp(entityBuf, "rsquo") == 0 || strcmp(entityBuf, "lsquo") == 0) {
    emitChar('\'');
  } else if (strcmp(entityBuf, "rdquo") == 0 || strcmp(entityBuf, "ldquo") == 0) {
    emitChar('"');
  } else {
    // Unknown entity: emit literally so no text is silently lost.
    emitChar('&');
    for (uint8_t i = 0; i < entityLen; i++) emitChar(entityBuf[i]);
    emitChar(';');
  }
}

void HtmlToText::emitUtf8(const uint32_t cp) {
  if (cp < 0x80) {
    emitChar(static_cast<char>(cp));
  } else if (cp < 0x800) {
    emitChar(static_cast<char>(0xC0 | (cp >> 6)));
    emitChar(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    emitChar(static_cast<char>(0xE0 | (cp >> 12)));
    emitChar(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    emitChar(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    emitChar(static_cast<char>(0xF0 | (cp >> 18)));
    emitChar(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    emitChar(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    emitChar(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string HtmlToText::resolveUrl(const std::string& hrefIn) const {
  if (hrefIn.empty() || hrefIn[0] == '#') return "";
  if (hrefIn.rfind("javascript:", 0) == 0 || hrefIn.rfind("mailto:", 0) == 0 || hrefIn.rfind("tel:", 0) == 0 ||
      hrefIn.rfind("data:", 0) == 0) {
    return "";
  }
  if (hrefIn.rfind("http://", 0) == 0 || hrefIn.rfind("https://", 0) == 0) return hrefIn;
  if (baseHost.empty()) return "";  // relative link but no usable base
  if (hrefIn.rfind("//", 0) == 0) return baseScheme + ":" + hrefIn;
  if (hrefIn[0] == '/') return baseHost + hrefIn;

  // Relative path: resolve against the page's directory with minimal ../ support.
  std::string dir = baseDir;
  size_t pos = 0;
  while (hrefIn.compare(pos, 3, "../") == 0) {
    // Pop one segment off dir (never past "scheme://host/").
    const size_t hostLen = baseHost.size() + 1;
    if (dir.size() > hostLen) {
      const size_t cut = dir.rfind('/', dir.size() - 2);
      if (cut != std::string::npos && cut + 1 >= hostLen) dir.resize(cut + 1);
    }
    pos += 3;
  }
  return dir + hrefIn.substr(pos);
}
