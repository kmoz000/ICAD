#include "icad_highlighter.hpp"

#include <QColor>
#include <QFont>

namespace icad::desktop {
namespace {

[[nodiscard]] auto make_format(QColor color, int weight = QFont::Normal,
                               bool italic = false) -> QTextCharFormat {
    QTextCharFormat result;
    result.setForeground(color);
    result.setFontWeight(weight);
    result.setFontItalic(italic);
    return result;
}

} // namespace

IcadHighlighter::IcadHighlighter(QTextDocument* document) : QSyntaxHighlighter{document} {
    const auto keyword = make_format(QColor{"#c084fc"}, QFont::DemiBold);
    const auto sketch = make_format(QColor{"#38bdf8"}, QFont::DemiBold);
    const auto feature = make_format(QColor{"#fb923c"}, QFont::DemiBold);
    const auto topology = make_format(QColor{"#facc15"}, QFont::DemiBold);
    const auto assembly = make_format(QColor{"#34d399"}, QFont::DemiBold);
    const auto material = make_format(QColor{"#f472b6"}, QFont::DemiBold);
    const auto parameter = make_format(QColor{"#a7f3d0"});
    const auto number = make_format(QColor{"#fda4af"});
    const auto string = make_format(QColor{"#fde68a"});
    const auto comment = make_format(QColor{"#64748b"}, QFont::Normal, true);
    const auto point_name = make_format(QColor{"#2dd4bf"}, QFont::DemiBold);
    const auto curve_name = make_format(QColor{"#22d3ee"}, QFont::DemiBold);
    const auto region_name = make_format(QColor{"#a3e635"}, QFont::DemiBold);
    const auto surface_name = make_format(QColor{"#d8b4fe"}, QFont::DemiBold);
    const auto object_name = make_format(QColor{"#fdba74"}, QFont::DemiBold);
    const auto event_name = make_format(QColor{"#6ee7b7"}, QFont::DemiBold);
    const auto parameter_name = make_format(QColor{"#5eead4"}, QFont::Bold);
    const auto options = QRegularExpression::CaseInsensitiveOption |
                         QRegularExpression::UseUnicodePropertiesOption;
    const auto expression = [options](const char* pattern) {
        return QRegularExpression{QString::fromLatin1(pattern), options};
    };

    rules_ = {
        {expression(R"(\b(REQUIRES|CAPABILITY|PROJECT|UNITS|END|IMPORT|INCLUDE|INJECT|AS|FROM|TO|ON|AT|BY|WITH|WHERE|ROLE|NEW|ADD|REMOVE|REQUIRE|ASSERT|EXPECT|QUERY|SELECT|SELECTION|NAMED|LET)\b)"), keyword},
        {expression(R"(\b(SKETCH|SHAPE|PLANE|LINE|ARC|CIRCLE|ELLIPSE|SPLINE|POLYGON|RECTANGLE|PROFILE|REGION|CONSTRUCTION|POINT|POINT2|POINT3|VECTOR|VECTOR3|CONSTRAINT|SOLVE|TRIM|OFFSET|CLOSE|START)\b)"), sketch},
        {expression(R"(\b(PAD|EXTRUDE|REVOLVE|SWEEP|LOFT|SHELL|FILLET|CHAMFER|ROUND|DRAFT|HOLE|POCKET|BOOLEAN|UNION|CUT|INTERSECT|PATTERN|MIRROR|SPLIT|THICKEN|FACE|EDGE|SURFACE|SOLID|BODY|COMPONENT|FEATURE)\b)"), feature},
        {expression(R"(\b(EDGES|EDGESET|FACES|VERTICES|LOOP|INNER|OUTER|TOP|BOTTOM|TANGENT|CONVEX|CONCAVE|PLANAR|CYLINDRICAL|CIRCULAR|PERSISTENT|ADJACENT|ADJACENT_TO|BOUNDARY)\b)"), topology},
        {expression(R"(\b(ASSEMBLY|INSTANCE|INTERFACE|CONNECT|AUTO|METHOD|STANDARD|FASTENER|MOUNT|FLANGE|SHAFT|BORE|PIN|HOLE|BEARING_SEAT|WELD_SEAM|BOND_FACE|BOLTED|SCREWED|PINNED|PRESS_FIT|SLIP_FIT|BEARING|WELDED|BRAZED|BONDED|MATE|JOINT|FIXED|REVOLUTE|PRISMATIC|COINCIDENT|CONCENTRIC|PARALLEL|PERPENDICULAR|DISTANCE|ANGLE|SCENE|TRACK|KEYFRAME|PLAY|EVENT|POSE|LIMIT)\b)"), assembly},
        {expression(R"(\b(MATERIAL|TEXTURE|APPLY|CONCRETE|STRUCTURAL_STEEL|STAINLESS_STEEL|ALUMINUM|ASPHALT|GLASS|WOOD|BRASS|COPPER|RUBBER|CARBON_FIBER|CERAMIC|NYLON|ABS|TITANIUM)\b)"), material},
        {expression(R"(\b(PARAMETER|PARAM|VARIABLE|DIAMETER|RADIUS|LENGTH|WIDTH|HEIGHT|DEPTH|THICKNESS|TOLERANCE|CLEARANCE|FIT)\b)"), parameter},
        {expression(R"(\b(?:PARAMETER|PARAM|ANGLE|LET)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), parameter_name, 1},
        {expression(R"(\b(?:POINT|POINT2|POINT3|VECTOR|VECTOR3)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), point_name, 1},
        {expression(R"(\b(?:LINE|ARC|CIRCLE|ELLIPSE|SPLINE)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), curve_name, 1},
        {expression(R"(\b(?:PROFILE|SHAPE|REGION|SELECTION)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), region_name, 1},
        {expression(R"(\b(?:FACE|SURFACE)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), surface_name, 1},
        {expression(R"(\b(?:FEATURE|BODY|SOLID|COMPONENT|INSTANCE)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), object_name, 1},
        {expression(R"(\b(?:SCENE|TRACK|EVENT|JOINT|MATE)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), event_name, 1},
        {expression(R"(\b(?:INTERFACE|CONNECT)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), event_name, 1},
        {expression(R"((?<![A-Za-z_])[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?(?:\s*(?:mm|cm|m|in|deg|rad|s|ms))?\b)"), number},
        {expression(R"("(?:\\.|[^"\\])*")"), string},
        {expression(R"(#.*$)"), comment},
    };
}

auto IcadHighlighter::highlightBlock(const QString& text) -> void {
    for (const auto& rule : rules_) {
        auto matches = rule.expression.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(static_cast<int>(match.capturedStart(rule.capture)),
                      static_cast<int>(match.capturedLength(rule.capture)), rule.format);
        }
    }
}

} // namespace icad::desktop
