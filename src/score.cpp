#include "score.hpp"

#include <lunasvg.h>
#include <toolkit.h>  // Verovio
#include <toolkitdef.h>

#include <filesystem>
#include <system_error>

#include "platform.hpp"

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
