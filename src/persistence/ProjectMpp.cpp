#include "persistence/ProjectMpp.h"
#include "persistence/ExchangePackage.h"
#include "domain/CalculationControl.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>
namespace jobs {
namespace {
constexpr qsizetype inputLimit = 32 * 1024 * 1024, xmlLimit = 16 * 1024 * 1024;
[[noreturn]] void bad(const QString &s) {
    throw AppError(Diagnostic{"MPP_IMPORT_INVALID", {s.toStdString()}, {}, "MPP_IMPORT_INVALID"});
}
QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path, qsizetype limit) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > limit)
        bad("파일을 읽을 수 없거나 한도를 넘었습니다: " + QFileInfo(path).fileName());
    auto bytes = f.read(limit + 1);
    if (f.error() != QFileDevice::NoError || bytes.size() > limit || !f.atEnd())
        bad("파일 읽기/크기 한도를 확인하세요.");
    return bytes;
}
void signature(const QByteArray &bytes) {
    if (bytes.size() < 512 || bytes.size() > inputLimit || !bytes.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1")))
        bad("MPP OLE 파일이 아니거나 32MiB 한도를 넘었습니다. XML 파일은 Project XML 가져오기를 사용하세요.");
}
struct Runtime {
    QString root, java, manifestHash;
};
Runtime runtime(const MppReaderOptions &options) {
    Runtime out;
    auto directory = options.runtimeDirectory;
    if (directory.isEmpty()) {
        const QDir app(QCoreApplication::applicationDirPath());
        const QStringList candidates{
#ifdef Q_OS_MACOS
            app.filePath("../Resources/mpp-reader"),
#endif
            app.filePath("mpp-reader"),
            app.filePath("../mpp-reader"), // Multi-configuration development executables (e.g. build/Release).
        };
        for (const auto &candidate : candidates)
            if (QFileInfo::exists(QDir(candidate).filePath("manifest.json"))) {
                directory = candidate;
                break;
            }
    }
    out.root = directory.isEmpty() ? QString{} : QFileInfo(directory).canonicalFilePath();
    if (out.root.isEmpty())
        bad("MPP reader 구성 요소가 없습니다. mpp-reader가 포함된 앱 배포본을 사용하세요.");
    const auto bytes = read(QDir(out.root).filePath("manifest.json"), 1024 * 1024);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    auto manifest = document.object();
    if (error.error != QJsonParseError::NoError || manifest["protocol"] != 2 || manifest["engine"] != "MPXJ" ||
        manifest["engineVersion"] != "16.10.0" || manifest["javaMajor"] != 21 || !manifest["files"].isObject())
        bad("MPP reader manifest 버전을 확인하세요.");
    auto files = manifest["files"].toObject();
#ifdef Q_OS_WIN
    const QString executable = "runtime/bin/java.exe";
#else
    const QString executable = "runtime/bin/java";
#endif
    if (files.size() > 2000 || !files.contains(executable) || !files.contains("bridge.jar") ||
        !files.contains("lib/mpxj.jar"))
        bad("MPP reader 파일 목록이 완전하지 않습니다.");
    for (const auto &name : files.keys()) {
        calculationCheckpoint();
        const auto path = QDir(out.root).filePath(name);
        const auto info = QFileInfo(path);
        if (QDir::isAbsolutePath(name) || name.contains('\\') || name.split('/').contains("..") ||
            !info.canonicalFilePath().startsWith(out.root + '/') || !files[name].isString() ||
            files[name].toString().size() != 64)
            bad("MPP reader 파일 경로/지문을 확인하세요.");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            bad("MPP reader 파일이 없습니다: " + name);
        QCryptographicHash h(QCryptographicHash::Sha256);
        while (!f.atEnd()) {
            calculationCheckpoint();
            auto chunk = f.read(256 * 1024);
            if (f.error() != QFileDevice::NoError)
                bad("MPP reader 파일 읽기 실패");
            h.addData(chunk);
        }
        if (QString::fromLatin1(h.result().toHex()) != files[name].toString())
            bad("MPP reader 파일 지문 불일치: " + name);
    }
    // Refuse unlisted JARs: Java wildcard classpaths must not load an unnoticed extra library.
    for (const auto &name : QDir(QDir(out.root).filePath("lib")).entryList({"*.jar"}, QDir::Files))
        if (!files.contains("lib/" + name))
            bad("목록에 없는 MPP reader 라이브러리입니다: " + name);
    out.java = QDir(out.root).filePath(executable);
    out.manifestHash = hash(bytes);
    return out;
}
QByteArray run(const Runtime &r, const QByteArray &input, int timeout) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        bad("MPP 읽기 임시 폴더를 만들 수 없습니다.");
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : {"JAVA_TOOL_OPTIONS", "_JAVA_OPTIONS", "JDK_JAVA_OPTIONS", "CLASSPATH"})
        environment.remove(key);
    child.setProcessEnvironment(environment);
    child.setWorkingDirectory(temporary.path());
    child.setProgram(r.java);
#ifdef Q_OS_WIN
    const auto separator = ";";
#else
    const auto separator = ":";
#endif
    child.setArguments({"-Xmx512m", "-Djava.awt.headless=true", "-Dfile.encoding=UTF-8",
                        "-Dlog4j2.statusLoggerLevel=OFF", "-Djava.io.tmpdir=" + temporary.path(), "-cp",
                        QDir(r.root).filePath("bridge.jar") + separator + QDir(r.root).filePath("lib/*"),
                        "app.toolmoa.ppm.MppBridge"});
    QElapsedTimer timer;
    timer.start();
    child.start();
    QByteArray output, errors;
    auto collect = [&] {
        output += child.readAllStandardOutput();
        errors += child.readAllStandardError();
        if (output.size() > xmlLimit + 8192 || errors.size() > 65536)
            bad("MPP reader 출력 한도를 넘었습니다.");
    };
    try {
        while (child.state() == QProcess::Starting) {
            child.waitForStarted(25);
            calculationCheckpoint();
            if (timer.elapsed() > timeout)
                bad("MPP reader 시작 시간이 초과되었습니다.");
        }
        if (child.state() != QProcess::Running)
            bad("MPP reader를 시작할 수 없습니다: " + child.errorString());
        if (child.write(input) != input.size())
            bad("MPP reader 입력 전달 실패");
        child.closeWriteChannel();
        while (!child.waitForFinished(25)) {
            calculationCheckpoint();
            collect();
            if (timer.elapsed() > timeout)
                bad("MPP 읽기 시간이 초과되었습니다.");
        }
        collect();
        calculationCheckpoint();
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
            bad("암호·손상·미지원 파일을 확인하세요. " + QString::fromUtf8(errors.left(2048)));
    } catch (...) {
        child.kill();
        child.waitForFinished(3000);
        throw;
    }
    return output;
}
QByteArray planXml(const QByteArray &bytes, const QString &currency) {
    QXmlStreamReader r(bytes);
    r.setEntityExpansionLimit(0);
    QByteArray out;
    QXmlStreamWriter w(&out);
    int depth = 0;
    bool written = false, timephaseValue = false;
    int timephase = 0;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isDTD() || r.isEntityReference())
            bad("MPP reader XML의 DTD/엔터티는 허용하지 않습니다.");
        if (r.isStartElement()) {
            ++depth;
            if (r.name() == "TimephasedData") {
                timephase = depth;
                timephaseValue = false;
            }
            if (timephase && depth == timephase + 1 && r.name() == "Value")
                timephaseValue = true;
            if (depth == 2 && r.name() == "CurrencyCode") {
                r.skipCurrentElement();
                w.writeTextElement("CurrencyCode", currency);
                --depth;
                written = true;
                continue;
            }
        }
        if (r.isEndElement()) {
            if (timephase && depth == timephase) {
                // MPXJ omits Value for zero work. Protocol2 emits source-validated regular plan rows only.
                if (!timephaseValue)
                    w.writeTextElement("Value", "PT0H0M0S");
                timephase = 0;
            }
            if (depth == 1 && !written)
                w.writeTextElement("CurrencyCode", currency);
            --depth;
        }
        if (!r.isEndDocument())
            w.writeCurrentToken(r);
        if (out.size() > xmlLimit)
            bad("MPP 계획 XML 크기 한도를 넘었습니다.");
    }
    if (r.hasError())
        bad("MPP reader XML이 올바르지 않습니다.");
    return out;
}
} // namespace
QByteArray readProjectMpp(const QString &path) {
    auto b = read(path, inputLimit);
    signature(b);
    return b;
}
ProjectMppExchange importProjectMpp(const QByteArray &bytes, const QString &sourceName,
                                    const MppReaderOptions &options) {
    EvaluationScope scope;
    calculationCheckpoint();
    signature(bytes);
    currencyDecimals(options.planCurrency.toStdString());
    if (options.timeoutMilliseconds < 1 || options.timeoutMilliseconds > 120000)
        bad("MPP 읽기 제한 시간을 확인하세요.");
    const auto reader = runtime(options);
    const auto output = run(reader, bytes, options.timeoutMilliseconds);
    if (!output.startsWith("PPM-MPP/2\n"))
        bad("MPP reader 응답 프로토콜이 일치하지 않습니다.");
    const auto end = output.indexOf('\n', 10);
    if (end < 10 || end > 8192)
        bad("MPP reader 메타데이터 한도를 확인하세요.");
    QJsonParseError error;
    const auto header = QJsonDocument::fromJson(output.mid(10, end - 10), &error).object();
    if (error.error != QJsonParseError::NoError || header.size() != 12 || header["protocol"] != 2 ||
        header["engine"] != "MPXJ" || header["engineVersion"] != "16.10.0" ||
        !QStringList{"MSProject.MPP8", "MSProject.MPP9", "MSProject.MPP12", "MSProject.MPP14"}.contains(
            header["format"].toString()))
        bad("MPP reader 형식/버전 메타데이터를 확인하세요.");
    for (const auto &key : {"tasks", "resources", "assignments", "calendars", "unassignedAssignments"}) {
        auto n = header[key].toDouble(-1);
        if (!header[key].isDouble() || n < 0 || n > 10001 || std::floor(n) != n)
            bad("MPP reader 행 수 한도를 확인하세요.");
    }
    if ((!header["sourceCurrency"].isNull() &&
         (!header["sourceCurrency"].isString() || header["sourceCurrency"].toString().size() > 16)) ||
        (!header["sourceName"].isNull() && !header["sourceName"].isString()))
        bad("MPP reader 원본 메타데이터 형식을 확인하세요.");
    const auto curves = header["planningCurves"].toObject();
    if (!header["planningCurves"].isObject() || curves.size() != 11 || curves["policy"] != "source-regular-minute-v1")
        bad("MPP 계획 곡선 정책을 확인하세요.");
    for (const auto &key :
         {"assignments", "rows", "excludedActual", "excludedOvertime", "missingManual", "roundedBoundaries",
          "projectedDurations", "rebasedOrigins", "roundedWorkRows", "progressPlaceholders"}) {
        const auto value = curves[key].toDouble(-1);
        if (!curves[key].isDouble() || value < 0 || value > 200000 || std::floor(value) != value)
            bad("MPP 곡선 행 수/투영 메타데이터를 확인하세요.");
    }
    if (curves["assignments"].toInt() > header["assignments"].toInt() || curves["rows"].toInt() > 100000)
        bad("MPP 곡선 행 수 한도를 확인하세요.");
    const auto xml = output.mid(end + 1);
    if (xml.isEmpty() || xml.size() > xmlLimit)
        bad("MPP reader XML 크기 한도를 확인하세요.");
    auto mapped = importProjectXml(planXml(xml, options.planCurrency), sourceName);
    if (header["sourceName"].isNull() || header["sourceName"].toString().trimmed().isEmpty())
        mapped.project.name = QFileInfo(sourceName).completeBaseName().toStdString();
    validate(mapped.project);
    auto report = mapped.report;
    report["contract"] = "ppm-mpp-read-v2";
    report["format"] = "Microsoft Project MPP";
    report["reader"] = header;
    report["planningCurves"] = curves;
    report["readerManifestSha256"] = reader.manifestHash;
    report["readerXmlSha256"] = hash(xml);
    report["source"] = QJsonObject{{"name", QFileInfo(sourceName).fileName()},
                                   {"sha256", hash(bytes)},
                                   {"bytes", bytes.size()},
                                   {"producer", "unverified"}};
    report["currencyPolicy"] = QJsonObject{{"source", header["sourceCurrency"]},
                                           {"nativePlan", options.planCurrency},
                                           {"financialValuesImported", false},
                                           {"exchangeRateApplied", false}};
    auto compatibility = report["compatibility"].toObject();
    compatibility["originalPreservation"] = "exact-mpp-bytes";
    report["compatibility"] = compatibility;
    auto diagnostics = report["diagnostics"].toArray();
    diagnostics.prepend(QJsonObject{
        {"path", "MPP"},
        {"message", "MPP 읽기는 기간 기반 계획의 지원 부분 투영입니다. 실적·원가/단가·기준선·표시/내장 데이터는 "
                    "원본 MPP에 보존하며 native 관측 원장으로 생성하지 않습니다. 미배정 placeholder 배정 " +
                        QString::number(header["unassignedAssignments"].toInt()) + "건을 제외했습니다."}});
    diagnostics.prepend(QJsonObject{
        {"path", "MPP/planningCurves"},
        {"message", QString("시간대별 계획 공수 %1배정/%2구간 · 원본 정규 공수만 채택 · 실제공수 %3/초과근무 %4배정 "
                            "제외 · 자료 없는 수동 곡선 %5배정. 정수 분 경계 올림 %6건/기간 창 투영 %7작업/시작 기준 "
                            "정렬 %8배정/1⁄100자원분 반올림 %9행. 원래 시간·기간과 이력은 original.mpp에 보존합니다.")
                        .arg(curves["assignments"].toInt())
                        .arg(curves["rows"].toInt())
                        .arg(curves["excludedActual"].toInt())
                        .arg(curves["excludedOvertime"].toInt())
                        .arg(curves["missingManual"].toInt())
                        .arg(curves["roundedBoundaries"].toInt())
                        .arg(curves["projectedDurations"].toInt())
                        .arg(curves["rebasedOrigins"].toInt())
                        .arg(curves["roundedWorkRows"].toInt())}});
    report["diagnostics"] = diagnostics;
    return {std::move(mapped.project), bytes, xml, report};
}
QString publishProjectMpp(const ProjectMppExchange &e, const QString &destination) {
    return publishPlanExchange(e.project, e.mpp, e.report, true, "mpp", destination);
}
QString projectMppPreview(const ProjectMppExchange &e) {
    auto text = projectXmlPreview({e.project, {}, e.report, true});
    text.replace("XML 원본 바이트", "MPP 원본 바이트").replace(".jobs", ".ppm");
    const auto curves = e.report["planningCurves"].toObject();
    const auto summary = QString("계획 곡선: %1배정/%2구간 · 실적 %3/초과근무 %4배정 제외\n분 경계 올림 %5건 · 기간 창 "
                                 "투영 %6작업 · 시작 기준 정렬 %7배정 · 공수 반올림 %8행\n계산은 PPM 논리 일정/09:00 "
                                 "날짜 시작 기준입니다. 원래 시각·기간·실적은 original.mpp에서 확인하세요.\n\n")
                             .arg(curves["assignments"].toInt())
                             .arg(curves["rows"].toInt())
                             .arg(curves["excludedActual"].toInt())
                             .arg(curves["excludedOvertime"].toInt())
                             .arg(curves["roundedBoundaries"].toInt())
                             .arg(curves["projectedDurations"].toInt())
                             .arg(curves["rebasedOrigins"].toInt())
                             .arg(curves["roundedWorkRows"].toInt());
    return "MPP 읽기 · " + e.report["reader"].toObject()["format"].toString() +
           " · MPXJ16.10.0\n원본 통화: " + e.report["reader"].toObject()["sourceCurrency"].toString("미정") +
           " · 계획 사본 통화: " + QString::fromStdString(e.project.currency) + " (원가·단가 변환/환산 없음)\n\n" +
           summary + text;
}
} // namespace jobs
