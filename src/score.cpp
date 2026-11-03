#include "score.hpp"

#include <lunasvg.h>
#include <toolkit.h>  // Verovio
#include <toolkitdef.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "platform.hpp"
#include "svgimage.hpp"

namespace fs = std::filesystem;

namespace {

EngraveResult failure(std::string message) { return {"", std::move(message)}; }

// Verovio's error messages from the last load, on one line ("" if it gave none).
std::string verovioLog(vrv::Toolkit& tk) {
    std::string log = tk.GetLog();
    while (!log.empty() && (log.back() == '\n' || log.back() == '\r' || log.back() == ' ')) log.pop_back();
    for (char& c : log)
        if (c == '\n' || c == '\r') c = ' ';
    return log;
}

}  // namespace

std::string defaultVerovioDataDir() {
    const std::string dir = executableDir();
    return (fs::path(dir.empty() ? "." : dir) / "verovio-data").string();
}

EngraveResult engraveScore(const std::string& path, const std::string& dataDir) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return failure("can't open " + path);
    if (!fs::is_directory(dataDir, ec))
        return failure("can't find Verovio's music fonts: no folder " + dataDir + " (use --verovio-data DIR)");

    // Collect Verovio's messages instead of letting it print them over the screen.
    vrv::EnableLogToBuffer(true);
    vrv::EnableLog(vrv::LOG_ERROR);

    // Symbols inside text (like the note in a tempo mark) use the music font as a text font.
    // lunasvg finds ordinary fonts (Times, for titles and lyrics) on the system, but not this one.
    static const bool leipzigLoaded =
        lunasvg_add_font_face_from_file("Leipzig", false, false, (fs::path(dataDir) / "Leipzig.ttf").string().c_str());
    (void)leipzigLoaded;

    vrv::Toolkit tk(/*initFont=*/false);
    if (!tk.SetResourcePath(dataDir)) return failure("couldn't load Verovio's music fonts from " + dataDir);

    // One long system and no page furniture: the pager does the scrolling.
    tk.SetOptions(R"({
        "breaks": "none",
        "header": "none",
        "footer": "none",
        "adjustPageWidth": true,
        "adjustPageHeight": true,
        "smuflTextFont": "none",
        "pageMarginTop": 50, "pageMarginBottom": 50, "pageMarginLeft": 50, "pageMarginRight": 50
    })");

    if (!tk.LoadFile(path)) {
        const std::string log = verovioLog(tk);
        return failure(path + ": not a score Verovio can read" + (log.empty() ? "" : " (" + log + ")"));
    }
    if (tk.GetPageCount() < 1) return failure(path + ": the score is empty");
    std::string svg = tk.RenderToSVG(1);
    if (svg.empty()) return failure(path + ": Verovio couldn't draw it");
    return {std::move(svg), ""};
}

namespace {

std::vector<lunasvg::Element> childElements(const lunasvg::Element& e) {
    std::vector<lunasvg::Element> out;
    for (const lunasvg::Node& n : e.children())
        if (n.isElement()) out.push_back(n.toElement());
    return out;
}

bool hasClass(const lunasvg::Element& e, const std::string& cls) {
    const std::string& all = e.getAttribute("class");
    for (size_t at = all.find(cls); at != std::string::npos; at = all.find(cls, at + 1)) {
        const bool startOk = at == 0 || all[at - 1] == ' ';
        const bool endOk = at + cls.size() == all.size() || all[at + cls.size()] == ' ';
        if (startOk && endOk) return true;
    }
    return false;
}

// A staff's lines: the <path> children it starts with (then come clefs, layers and so on).
std::vector<lunasvg::Box> staffLines(const lunasvg::Element& staff) {
    std::vector<lunasvg::Box> lines;
    for (const lunasvg::Element& c : childElements(staff)) {
        if (c.getAttribute("d").empty()) break;
        lines.push_back(c.getGlobalBoundingBox());
    }
    return lines;
}

}  // namespace

std::optional<ScoreLayout> findScoreLayout(const SvgDocument& doc) {
    const lunasvg::ElementList measures = doc.lunasvgDocument().querySelectorAll(".measure");
    if (measures.empty()) return std::nullopt;

    ScoreLayout layout;
    // Staves, from the first bar: the middle of each staff's top and bottom line.
    std::vector<double> spaces;
    for (const lunasvg::Element& staff : childElements(measures.front())) {
        if (!hasClass(staff, "staff")) continue;
        const std::vector<lunasvg::Box> lines = staffLines(staff);
        if (lines.empty()) continue;
        const double top = lines.front().y + lines.front().h / 2, bottom = lines.back().y + lines.back().h / 2;
        layout.staves.push_back({top, bottom, 0, 0});
        if (lines.size() >= 2) spaces.push_back((bottom - top) / double(lines.size() - 1));
    }
    if (layout.staves.empty()) return std::nullopt;
    if (!spaces.empty()) layout.staffSpace = spaces.front();
    for (size_t i = 0; i < layout.staves.size(); ++i) {
        ScoreLayout::Staff& s = layout.staves[i];
        s.bandTop = i == 0 ? 0 : (layout.staves[i - 1].bottom + s.top) / 2;
        s.bandBottom = i + 1 == layout.staves.size() ? doc.height() : (s.bottom + layout.staves[i + 1].top) / 2;
    }

    // Bars: where the first staff's lines start and end in each.
    for (const lunasvg::Element& m : measures) {
        for (const lunasvg::Element& staff : childElements(m)) {
            if (!hasClass(staff, "staff")) continue;
            const std::vector<lunasvg::Box> lines = staffLines(staff);
            if (lines.empty()) continue;
            layout.barStarts.push_back(lines.front().x);
            layout.end = std::max(layout.end, double(lines.front().x + lines.front().w));
            break;
        }
    }
    if (layout.barStarts.empty()) return std::nullopt;
    return layout;
}
