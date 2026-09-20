#include "tts/RacingTextNormalizer.h"

#include <QRegularExpression>

namespace raceengineer {

namespace {

// Vietnamese digit words for 0-9
const QString kDigits[] = {
    QStringLiteral("không"), QStringLiteral("một"), QStringLiteral("hai"),
    QStringLiteral("ba"), QStringLiteral("bốn"), QStringLiteral("năm"),
    QStringLiteral("sáu"), QStringLiteral("bảy"), QStringLiteral("tám"),
    QStringLiteral("chín")
};

// Convert an integer to Vietnamese words following natural Vietnamese speech rules.
// Supports up to 999,999,999 (handles mười/mươi, mốt/tư/lăm, linh/lẻ).
QString numberToVietnamese(long long n)
{
    if (n < 0) return QStringLiteral("âm ") + numberToVietnamese(-n);
    if (n <= 10) {
        if (n == 10) return QStringLiteral("mười");
        return kDigits[n];
    }

    // 11–19: mười + unit (special: 11="mười một", 14="mười bốn", 15="mười lăm")
    if (n < 20) {
        const int unit = n % 10;
        if (unit == 5) return QStringLiteral("mười lăm");
        return QStringLiteral("mười ") + kDigits[unit];
    }

    // 20–99
    if (n < 100) {
        const int tens = n / 10;
        const int unit = n % 10;
        QString result = kDigits[tens] + QStringLiteral(" mươi");
        if (unit == 0) return result;
        if (unit == 1) return result + QStringLiteral(" mốt");
        if (unit == 4) return result + QStringLiteral(" tư");
        if (unit == 5) return result + QStringLiteral(" lăm");
        return result + QStringLiteral(" ") + kDigits[unit];
    }

    // 100–999
    if (n < 1000) {
        const int hundreds = n / 100;
        const int remainder = n % 100;
        QString result = kDigits[hundreds] + QStringLiteral(" trăm");
        if (remainder == 0) return result;
        if (remainder < 10)
            return result + QStringLiteral(" lẻ ") + kDigits[remainder];
        return result + QStringLiteral(" ") + numberToVietnamese(remainder);
    }

    // 1000–999,999 (thousands)
    if (n < 1000000) {
        const long long thousands = n / 1000;
        const long long remainder = n % 1000;
        QString result = numberToVietnamese(thousands) + QStringLiteral(" nghìn");
        if (remainder == 0) return result;
        if (remainder < 10)
            return result + QStringLiteral(" không trăm lẻ ") + kDigits[remainder];
        if (remainder < 100)
            return result + QStringLiteral(" không trăm ") + numberToVietnamese(remainder);
        return result + QStringLiteral(" ") + numberToVietnamese(remainder);
    }

    // 1,000,000–999,999,999 (millions)
    if (n < 1000000000LL) {
        const long long millions = n / 1000000;
        const long long remainder = n % 1000000;
        QString result = numberToVietnamese(millions) + QStringLiteral(" triệu");
        if (remainder == 0) return result;
        return result + QStringLiteral(" ") + numberToVietnamese(remainder);
    }

    // Fallback: spell out each digit for very large numbers
    QString result;
    const QString s = QString::number(n);
    for (const QChar c : s) {
        if (!result.isEmpty()) result += QStringLiteral(" ");
        result += kDigits[c.digitValue()];
    }
    return result;
}

// Convert a digit sequence that may have leading zeros (e.g. fractional parts of decimals)
// Examples: "05" -> "không năm", "005" -> "không không năm", "025" -> "không hai mươi lăm"
QString digitsOrNumberToVietnamese(const QString& str)
{
    if (str.isEmpty()) return QString();
    if (str.length() > 1 && str.startsWith(QLatin1Char('0'))) {
        int zeroCount = 0;
        while (zeroCount < str.length() && str[zeroCount] == QLatin1Char('0')) {
            ++zeroCount;
        }
        QString result;
        for (int i = 0; i < zeroCount; ++i) {
            if (!result.isEmpty()) result += QStringLiteral(" ");
            result += QStringLiteral("không");
        }
        if (zeroCount < str.length()) {
            const QString rem = str.mid(zeroCount);
            bool ok = false;
            const long long val = rem.toLongLong(&ok);
            if (ok) {
                result += QStringLiteral(" ") + numberToVietnamese(val);
            } else {
                for (const QChar c : rem) {
                    result += QStringLiteral(" ") + kDigits[c.digitValue()];
                }
            }
        }
        return result;
    }

    bool ok = false;
    const long long value = str.toLongLong(&ok);
    if (!ok) return str;
    return numberToVietnamese(value);
}

// Replace a matched integer capture group with Vietnamese words
QString replaceNumberMatch(const QRegularExpressionMatch& match)
{
    return digitsOrNumberToVietnamese(match.captured(0));
}

} // namespace

QString RacingTextNormalizer::normalize(const QString& text)
{
    if (text.trimmed().isEmpty()) return QString();

    QString result = text;

    // 1. Strip Markdown formatting (bold, italic, inline code, headers)
    result.replace(QRegularExpression(QStringLiteral("[*`_#]")), QString());

    // 2. Normalize ellipses and multiple punctuation marks
    result.replace(QRegularExpression(QStringLiteral("\\.{2,}")), QStringLiteral("."));
    result.replace(QRegularExpression(QStringLiteral("!{2,}")), QStringLiteral("!"));
    result.replace(QRegularExpression(QStringLiteral("\\?{2,}")), QStringLiteral("?"));

    // 3. Lap times: e.g. "1:42.350" -> "1 phút 42 phẩy 350 giây", "1:42" -> "1 phút 42 giây"
    result.replace(QRegularExpression(QStringLiteral("\\b(\\d{1,2}):(\\d{2})\\.(\\d+)\\b")),
                   QStringLiteral("\\1 phút \\2 phẩy \\3 giây"));
    result.replace(QRegularExpression(QStringLiteral("\\b(\\d{1,2}):(\\d{2})\\b")),
                   QStringLiteral("\\1 phút \\2 giây"));

    // 4. Signed time deltas: e.g. "+0.5s", "+1.2 s", "-0.8s"
    result.replace(QRegularExpression(QStringLiteral("\\+([0-9]+(?:[\\.,][0-9]+)?)\\s*s\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("nhanh hơn \\1 giây"));
    result.replace(QRegularExpression(QStringLiteral("-([0-9]+(?:[\\.,][0-9]+)?)\\s*s\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("chậm hơn \\1 giây"));

    // 5. Plain seconds: e.g. "2.5s", "10s"
    result.replace(QRegularExpression(QStringLiteral("\\b([0-9]+(?:[\\.,][0-9]+)?)\\s*s\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 giây"));

    // 6. Negative numbers: e.g. "-5°C" -> "âm 5°C", "-2.1" -> "âm 2.1", "-10" -> "âm 10"
    // Runs after signed deltas (+0.5s / -0.8s) so deltas keep "nhanh hơn / chậm hơn".
    result.replace(QRegularExpression(QStringLiteral("(?<=^|[\\s(])-(?=\\d)")),
                   QStringLiteral("âm "));

    // 7. Telemetry units
    // Speed
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:km/h|kph)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 ki lô mét trên giờ"));
    result.replace(QRegularExpression(QStringLiteral("\\b(?:km/h|kph)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("ki lô mét trên giờ"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*mph\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 dặm trên giờ"));
    result.replace(QRegularExpression(QStringLiteral("\\bmph\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("dặm trên giờ"));

    // Distance & Dimensions
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*km\\b(?!/)"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 ki lô mét"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*mm\\b(?!\\w)")),
                   QStringLiteral("\\1 mi li mét"));
    result.replace(QRegularExpression(QStringLiteral("\\bmm\\b(?!\\w)")),
                   QStringLiteral("mi li mét"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*cm\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 xen ti mét"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*m\\b(?!/|\\w)"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 mét"));

    // Temperature
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:°C|degC|deg c)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 độ xê"));
    result.replace(QRegularExpression(QStringLiteral("(?:°C|degC|deg c)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("độ xê"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:°F|degF|deg f)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 độ ép"));
    result.replace(QRegularExpression(QStringLiteral("(?:°F|degF|deg f)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("độ ép"));
    result.replace(QRegularExpression(QStringLiteral("°")),
                   QStringLiteral(" độ"));

    // Pressure
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*kPa\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 ki lô pát can"));
    result.replace(QRegularExpression(QStringLiteral("\\bkPa\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("ki lô pát can"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*bar\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 ba"));
    result.replace(QRegularExpression(QStringLiteral("\\bbar\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("ba"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*psi\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 pi ét ai"));
    result.replace(QRegularExpression(QStringLiteral("\\bpsi\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("pi ét ai"));

    // Engine & Dynamics
    result.replace(QRegularExpression(QStringLiteral("(\\d+)\\s*(?:rpm|RPM|r/min)\\b")),
                   QStringLiteral("\\1 vòng trên phút"));
    result.replace(QRegularExpression(QStringLiteral("\\b(?:rpm|RPM|r/min)\\b")),
                   QStringLiteral("vòng trên phút"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:Nm|N·m)\\b")),
                   QStringLiteral("\\1 niu tơn mét"));
    result.replace(QRegularExpression(QStringLiteral("\\b(?:Nm|N·m)\\b")),
                   QStringLiteral("niu tơn mét"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:hp|bhp)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 mã lực"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*kg\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 ki lô gam"));
    result.replace(QRegularExpression(QStringLiteral("\\bkg\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("ki lô gam"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*[Gg]\\b")),
                   QStringLiteral("\\1 Gờ"));

    // Fuel & Fluids
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:l|liters?)/(?:lap|vòng)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 lít trên vòng"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*(?:L(?!í)|liters?)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 lít"));

    // Electrical & Time units
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*ms\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1 mi li giây"));
    result.replace(QRegularExpression(QStringLiteral("\\bms\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("mi li giây"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*V\\b")),
                   QStringLiteral("\\1 Vôn"));

    // Percentage
    result.replace(QRegularExpression(QStringLiteral("(\\d+(?:[\\.,]\\d+)?)\\s*%")),
                   QStringLiteral("\\1 phần trăm"));

    // 8. Decimals: e.g. "2.1" -> "2 phẩy 1", "0.05" -> "0 phẩy 05", "2,5" -> "2 phẩy 5"
    // Applied after units have consumed their numeric prefixes.
    result.replace(QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)")),
                   QStringLiteral("\\1 phẩy \\2"));
    result.replace(QRegularExpression(QStringLiteral("(\\d+),(\\d{1,2})(?=[^\\d]|$)")),
                   QStringLiteral("\\1 phẩy \\2"));

    // 8. Positions & Classes
    result.replace(QRegularExpression(QStringLiteral("\\bP([1-9]|1\\d|2\\d)\\b")),
                   QStringLiteral("vị trí \\1"));
    result.replace(QRegularExpression(QStringLiteral("\\bGT3\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("G T ba"));
    result.replace(QRegularExpression(QStringLiteral("\\bGT4\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("G T bốn"));
    result.replace(QRegularExpression(QStringLiteral("\\bLMP2\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("L M P hai"));
    result.replace(QRegularExpression(QStringLiteral("\\bF1\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("F một"));

    // 9. Racing terminology & compound phrases
    // Box / Pit
    result.replace(QRegularExpression(QStringLiteral("\\bbox[,\\s]+box[,\\s]+box\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vào pít, vào pít, vào pít"));
    result.replace(QRegularExpression(QStringLiteral("\\bbox[,\\s]+box\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vào pít, vào pít"));
    result.replace(QRegularExpression(QStringLiteral("vào\\s+box\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vào pít"));
    result.replace(QRegularExpression(QStringLiteral("\\bbox\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vào pít"));
    result.replace(QRegularExpression(QStringLiteral("\\bpit\\s*stop\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vào trạm pít"));
    result.replace(QRegularExpression(QStringLiteral("\\bpit\\s*limiter\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("giới hạn tốc độ pít"));
    result.replace(QRegularExpression(QStringLiteral("\\bpits?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("pít"));

    // Laps & Gaps & Sectors
    result.replace(QRegularExpression(QStringLiteral("\\blap\\s*time\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("thời gian vòng"));
    result.replace(QRegularExpression(QStringLiteral("\\blaps?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vòng"));
    result.replace(QRegularExpression(QStringLiteral("\\bgaps?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("khoảng cách"));
    result.replace(QRegularExpression(QStringLiteral("\\bdelta\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("chênh lệch"));
    result.replace(QRegularExpression(QStringLiteral("\\bsectors?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("phân đoạn"));

    // Race driving terms
    result.replace(QRegularExpression(QStringLiteral("\\bpushing\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("đang tăng tốc"));
    result.replace(QRegularExpression(QStringLiteral("\\bpush\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("tăng tốc"));
    result.replace(QRegularExpression(QStringLiteral("\\bpace\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("tốc độ chạy"));
    result.replace(QRegularExpression(QStringLiteral("\\bundersteer\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("thiếu lái"));
    result.replace(QRegularExpression(QStringLiteral("\\boversteer\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("thừa lái"));
    result.replace(QRegularExpression(QStringLiteral("\\bapex\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("đỉnh cua"));
    result.replace(QRegularExpression(QStringLiteral("\\bkerbs?|curbs?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("gờ giảm tốc"));
    result.replace(QRegularExpression(QStringLiteral("\\bturns?\\s*(\\d+)\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("khúc cua \\1"));
    result.replace(QRegularExpression(QStringLiteral("\\btraffic\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("xe phía trước"));
    result.replace(QRegularExpression(QStringLiteral("\\bstint\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("giai đoạn chạy"));

    // Flags & Safety
    result.replace(QRegularExpression(QStringLiteral("\\bsafety\\s*car\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("xe an toàn"));
    result.replace(QRegularExpression(QStringLiteral("\\bvsc\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("xe an toàn ảo"));
    result.replace(QRegularExpression(QStringLiteral("\\byellow\\s*flag\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("cờ vàng"));
    result.replace(QRegularExpression(QStringLiteral("\\bgreen\\s*flag\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("cờ xanh"));
    result.replace(QRegularExpression(QStringLiteral("\\bblue\\s*flag\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("cờ xanh dương"));
    result.replace(QRegularExpression(QStringLiteral("\\bchequered\\s*flag\\b|\\bcheckered\\s*flag\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("cờ ca rô"));

    // Tyres
    result.replace(QRegularExpression(QStringLiteral("\\btyres?|tires?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp"));
    result.replace(QRegularExpression(QStringLiteral("\\bsofts?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp mềm"));
    result.replace(QRegularExpression(QStringLiteral("\\bmediums?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp trung bình"));
    result.replace(QRegularExpression(QStringLiteral("\\bhards?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp cứng"));
    result.replace(QRegularExpression(QStringLiteral("\\binters?|intermediates?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp bán trợt"));
    result.replace(QRegularExpression(QStringLiteral("\\bwets?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp mưa"));
    result.replace(QRegularExpression(QStringLiteral("\\bslicks?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("lốp trơn"));

    // Systems
    result.replace(QRegularExpression(QStringLiteral("\\bdrs\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("đê rờ ét"));
    result.replace(QRegularExpression(QStringLiteral("\\bers\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("e rờ ét"));
    result.replace(QRegularExpression(QStringLiteral("\\btcs?\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("kiểm soát lực kéo"));
    result.replace(QRegularExpression(QStringLiteral("\\babs\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("chống bó cứng phanh"));
    result.replace(QRegularExpression(QStringLiteral("\\bfuel\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("nhiên liệu"));

    // Sessions & Comms
    result.replace(QRegularExpression(QStringLiteral("\\bquali|qualifying\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("phân hạng"));
    result.replace(QRegularExpression(QStringLiteral("\\bpractice\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("luyện tập"));
    result.replace(QRegularExpression(QStringLiteral("\\bcopy\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("đã rõ"));
    result.replace(QRegularExpression(QStringLiteral("\\bradio\\s*check\\b"), QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("kiểm tra sóng vô tuyến"));
    // 11. Convert remaining bare integers to Vietnamese words so TTS reads them naturally.
    //     Applied last so earlier unit rules (km/h, °C, %, etc.) have already consumed
    //     their numeric operands and won't be double-converted.
    {
        static const QRegularExpression bareNumber(QStringLiteral("\\b(\\d+)\\b"));
        QString converted;
        converted.reserve(result.size() + result.size() / 4);
        int lastEnd = 0;
        auto it = bareNumber.globalMatch(result);
        while (it.hasNext()) {
            const auto m = it.next();
            converted += result.mid(lastEnd, m.capturedStart() - lastEnd);
            converted += replaceNumberMatch(m);
            lastEnd = m.capturedEnd();
        }
        converted += result.mid(lastEnd);
        result = converted;
    }

    // 12. Clean up any remaining hyphens between words/letters (e.g. ca-rô -> ca rô, niu-tơn -> niu tơn)
    //     so Vietnamese G2P phonemizer won't choke and drop trailing syllables.
    result.replace(QRegularExpression(QStringLiteral("(?<=\\p{L})-(?=\\p{L})")), QStringLiteral(" "));

    // Collapse multiple whitespace
    result.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return result.trimmed();
}

} // namespace raceengineer
