#include "ui/ServerDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/EditorUi.h"
#include "ui/SandboxAccess.h"
#include "collaboration/ServerClient.h"
#include "persistence/ProjectFile.h"
#include "formatting/QtDiagnosticText.h"
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
namespace jobs {
namespace {
struct State {
    QJsonObject current, preview;
    std::uint64_t epoch = 0;
    bool dirty = false, applying = false;
};
QString q(const std::string &v) {
    return QString::fromStdString(v);
}
QPushButton *button(QHBoxLayout *layout, const QString &label, const char *name) {
    auto *result = new QPushButton(label);
    result->setObjectName(name);
    layout->addWidget(result);
    return result;
}
} // namespace
ServerDialog::ServerDialog(DesktopWorkspace &workspace, QWidget *parent) : QDialog(parent) {
    setWindowTitle("서버 인증·실시간 협업·주기 실행");
    setObjectName("serverDialog");
    resize(1180, 800);
    auto *client = new ServerClient(this);
    auto state = std::make_shared<State>();
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "서버 계정으로 인증하고 서버 원본을 편집합니다. 버전이 바뀌면 작성 중인 입력을 보존하고 충돌을 표시합니다.\n"
        "기존 로컬 파일은 명시적으로 등록·다운로드하며 자동으로 덮어쓰지 않습니다. 서버 주소와 비밀번호는 저장하지 "
        "않습니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *credentials = new QHBoxLayout;
    layout->addLayout(credentials);
    auto *url = new QLineEdit("http://127.0.0.1:8765");
    url->setObjectName("serverUrl");
    url->setPlaceholderText("HTTPS 서버 주소");
    auto *user = new QLineEdit;
    user->setObjectName("serverUser");
    user->setPlaceholderText("사용자");
    auto *password = new QLineEdit;
    password->setObjectName("serverPassword");
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText("비밀번호");
    credentials->addWidget(url, 2);
    credentials->addWidget(user);
    credentials->addWidget(password);
    auto *login = button(credentials, "로그인", "serverLogin");
    auto *logout = button(credentials, "로그아웃", "serverLogout");
    auto *status = new QLabel("서버 계정으로 로그인하세요.");
    status->setObjectName("serverStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    const auto showError = [status](const ServerClient::Result &r) {
        if (!r.ok())
            status->setText(r.error);
        return !r.ok();
    };
    auto *selection = new QHBoxLayout;
    layout->addLayout(selection);
    auto *projects = new QComboBox;
    projects->setObjectName("serverProjects");
    selection->addWidget(projects, 1);
    auto *refreshList = button(selection, "목록 새로고침", "serverList");
    auto *open = button(selection, "프로젝트 열기", "serverOpen");
    auto *reload = button(selection, "서버 원본 다시 읽기", "serverReload");
    auto *download = button(selection, "네이티브 사본 저장…", "serverDownload");
    auto *source = new QLabel;
    source->setObjectName("serverSource");
    source->setTextFormat(Qt::PlainText);
    layout->addWidget(source);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("serverTasks");
    table->setHorizontalHeaderLabels({"작업 이름", "기간 (근무 분·공란=미정)", "안정 ID"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table, 2);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto *preview = button(actions, "변경 미리보기", "serverPreview");
    auto *apply = button(actions, "검토한 변경 적용", "serverApply");
    apply->setEnabled(false);
    auto *receipt = new QPlainTextEdit;
    receipt->setObjectName("serverReceipt");
    receipt->setReadOnly(true);
    layout->addWidget(receipt, 1);
    const auto invalidate = [state, apply] {
        state->preview = {};
        apply->setEnabled(false);
    };
    const auto listing = [=] {
        client->request("GET", "/projects", {}, [=](ServerClient::Result r) {
            if (showError(r))
                return;
            const auto selected = projects->currentData();
            projects->clear();
            for (const auto &p : r.json.value("projects").toArray()) {
                const auto o = p.toObject();
                projects->addItem(o.value("name").toString() + " · v" + QString::number(o.value("version").toInt()),
                                  o.value("id").toString());
            }
            if (projects->findData(selected) >= 0)
                projects->setCurrentIndex(projects->findData(selected));
        });
    };
    const auto load = [=](bool discard) {
        if (state->dirty && !discard) {
            status->setText("작성 중인 입력이 있습니다. 미리보기로 검토하거나 서버 원본 다시 읽기를 선택하세요.");
            return;
        }
        const auto id = projects->currentData().toString();
        if (id.isEmpty())
            return;
        const auto epoch = state->epoch;
        client->request("GET", ServerClient::projectPath(id), {}, [=](ServerClient::Result r) {
            if (showError(r))
                return;
            if (epoch != state->epoch || id != projects->currentData().toString()) {
                status->setText("입력이 변경되어 서버 원본으로 덮어쓰지 않았습니다.");
                return;
            }
            if (state->current.value("id") == r.json.value("id") &&
                state->current.value("version").toDouble() > r.json.value("version").toDouble())
                return;
            const QSignalBlocker blocked(table);
            state->current = r.json;
            state->dirty = false;
            invalidate();
            const auto summary = r.json.value("summary").toObject();
            const auto tasks = summary.value("tasks").toArray();
            table->setRowCount(tasks.size());
            for (int row = 0; row < tasks.size(); ++row) {
                const auto t = tasks[row].toObject();
                table->setItem(row, 0, new QTableWidgetItem(t.value("name").toString()));
                table->setItem(
                    row, 1,
                    new QTableWidgetItem(t.value("durationMinutes").isNull()
                                             ? QString{}
                                             : QString::number(qint64(t.value("durationMinutes").toDouble()))));
                auto *stable = new QTableWidgetItem(t.value("taskId").toString());
                stable->setFlags(stable->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 2, stable);
            }
            source->setText(QString("%1 · 서버 버전 %2 · 입력 revision %3")
                                .arg(r.json.value("name").toString())
                                .arg(r.json.value("version").toInt())
                                .arg(qint64(summary.value("revision").toDouble())));
            status->setText("서버 원본을 읽었습니다. 실시간 변경을 수신합니다.");
        });
    };
    connect(table, &QTableWidget::itemChanged, this, [=] {
        ++state->epoch;
        state->dirty = true;
        invalidate();
    });
    const auto batch = [state, table]() -> QJsonObject {
        if (state->current.isEmpty())
            throw std::runtime_error("서버 프로젝트를 먼저 여세요.");
        const auto summary = state->current.value("summary").toObject();
        const auto original = summary.value("tasks").toArray();
        QJsonArray operations;
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto t = original[row].toObject();
            QJsonObject op{{"taskId", t.value("taskId")}};
            const auto name = table->item(row, 0)->text();
            const auto duration = table->item(row, 1)->text().trimmed();
            if (name != t.value("name").toString())
                op["name"] = name;
            QJsonValue value(QJsonValue::Null);
            if (!duration.isEmpty()) {
                bool ok = false;
                const auto n = duration.toLongLong(&ok);
                if (!ok || n < 0 || n > 5000000)
                    throw std::runtime_error("기간은 0~5000000의 정수 분입니다.");
                value = qint64(n);
            }
            if (value != t.value("durationMinutes"))
                op["durationMinutes"] = value;
            if (op.size() > 1)
                operations.append(op);
        }
        if (operations.isEmpty())
            throw std::runtime_error("변경된 작업이 없습니다.");
        return {{"expectedVersion", state->current.value("version")},
                {"request", QJsonObject{{"format", "PPM-AUTOMATION-1"},
                                        {"documentId", state->current.value("id")},
                                        {"revision", summary.value("revision")},
                                        {"requestId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                        {"operations", operations}}}};
    };
    const auto guard = [status](const std::function<void()> &f) {
        try {
            f();
        } catch (const std::exception &e) {
            status->setText(qErrorText(e));
        }
    };
    connect(login, &QPushButton::clicked, this, [=] {
        guard([&] {
            client->setEndpoint(QUrl(url->text().trimmed()));
            invalidate();
            ++state->epoch;
            state->current = {};
            state->dirty = false;
            state->applying = false;
            table->setRowCount(0);
            table->setEnabled(true);
            projects->clear();
            receipt->clear();
            source->clear();
            client->login(user->text(), password->text(), [=](ServerClient::Result r) {
                login->setEnabled(true);
                if (showError(r))
                    return;
                status->setText(r.json.value("user").toString() + " · 로그인했습니다.");
                listing();
                client->follow(
                    [=](const QJsonObject &changes) {
                        for (const auto &v : changes.value("events").toArray()) {
                            const auto e = v.toObject();
                            const auto kind = e.value("kind").toString();
                            if (kind == "project-created" || kind == "permission-changed")
                                listing();
                            if (kind == "project-changed" &&
                                e.value("version").toDouble() <= state->current.value("version").toDouble())
                                continue;
                            if (e.value("projectId") == state->current.value("id") &&
                                (kind == "project-changed" || kind == "permission-changed")) {
                                invalidate();
                                if (state->dirty || state->applying)
                                    status->setText("서버 원본/권한이 변경되었습니다. 작성 중인 입력을 보존했습니다. "
                                                    "다시 읽고 대조하세요.");
                                else
                                    load(false);
                            }
                        }
                    },
                    [status](const QString &error) { status->setText(error + " · 연결 재시도 중"); });
            });
            password->clear();
            login->setEnabled(false);
        });
    });
    connect(logout, &QPushButton::clicked, this, [=] {
        client->logout([=](ServerClient::Result r) {
            invalidate();
            ++state->epoch;
            state->dirty = false;
            state->current = {};
            table->setRowCount(0);
            projects->clear();
            receipt->clear();
            source->clear();
            status->setText(r.ok() ? "로그아웃했습니다." : r.error);
        });
    });
    connect(refreshList, &QPushButton::clicked, this, listing);
    connect(open, &QPushButton::clicked, this, [load] { load(false); });
    connect(reload, &QPushButton::clicked, this, [load] { load(true); });
    connect(preview, &QPushButton::clicked, this, [=] {
        guard([&] {
            invalidate();
            const auto data = batch();
            const auto epoch = state->epoch;
            const auto id = state->current.value("id").toString();
            client->request("POST", ServerClient::projectPath(id) + "/preview", data, [=](ServerClient::Result r) {
                if (showError(r))
                    return;
                if (state->epoch != epoch || state->current.value("id") != id) {
                    status->setText("입력이 변경되었습니다. 미리보기를 다시 실행하세요.");
                    return;
                }
                state->preview = r.json;
                receipt->setPlainText(QJsonDocument(r.json.value("receipt").toObject()).toJson());
                apply->setEnabled(true);
            });
        });
    });
    connect(apply, &QPushButton::clicked, this, [=] {
        if (state->preview.isEmpty() || state->applying)
            return;
        const auto ticket = state->preview.value("previewId");
        invalidate();
        state->applying = true;
        table->setEnabled(false);
        client->request("POST", ServerClient::projectPath(state->current.value("id").toString()) + "/apply",
                        {{"previewId", ticket}}, [=](ServerClient::Result r) {
                            state->applying = false;
                            table->setEnabled(true);
                            if (showError(r))
                                return;
                            state->dirty = false;
                            receipt->setPlainText(QJsonDocument(r.json).toJson());
                            load(true);
                        });
    });
    auto *files = new QHBoxLayout;
    layout->addLayout(files);
    auto *local = new QComboBox;
    local->setObjectName("serverLocalProject");
    files->addWidget(local, 1);
    for (const auto &p : workspace.snapshots())
        local->addItem(q(p->name), q(p->documentId));
    auto *upload = button(files, "선택 로컬 프로젝트 등록", "serverUpload");
    connect(upload, &QPushButton::clicked, this, [=, &workspace] {
        guard([&] {
            auto *doc = workspace.document(local->currentData().toString().toStdString());
            if (!doc || !doc->snapshot())
                throw std::runtime_error("열린 로컬 프로젝트를 선택하세요.");
            QTemporaryDir temp;
            const auto path = temp.filePath("snapshot.ppm");
            JsonProjectStore store;
            store.save(path.toStdString(), *doc->snapshot(), {});
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("프로젝트 사본을 읽지 못했습니다.");
            client->upload(file.readAll(), [=](ServerClient::Result r) {
                if (showError(r))
                    return;
                status->setText("서버에 등록했습니다.");
                listing();
            });
        });
    });
    connect(download, &QPushButton::clicked, this, [=, this] {
        if (state->current.isEmpty())
            return;
        const auto path = QFileDialog::getSaveFileName(this, "서버 네이티브 사본 저장", {}, "PPM 프로젝트 (*.ppm)");
        if (path.isEmpty() || !authorizeSandboxFile(this, path))
            return;
        client->download(state->current.value("id").toString(), [=](ServerClient::Result r) {
            guard([&] {
                if (showError(r))
                    return;
                const auto project = decodeProjectFile(r.bytes);
                JsonProjectStore store;
                auto lease = store.lock(path.toStdString());
                store.save(path.toStdString(), project, {});
                status->setText("네이티브 사본을 새 파일에 저장했습니다.");
            });
        });
    });
    auto *schedule = new QHBoxLayout;
    layout->addLayout(schedule);
    auto *interval = new QSpinBox;
    interval->setObjectName("serverInterval");
    interval->setRange(1, 43200);
    interval->setValue(60);
    auto *start = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(60));
    start->setObjectName("serverStart");
    start->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    schedule->addWidget(new QLabel("주기 (분)"));
    schedule->addWidget(interval);
    schedule->addWidget(new QLabel("첫 실행 (현지 시각)"));
    schedule->addWidget(start);
    auto *createJob = button(schedule, "현재 변경으로 예약", "serverSchedule");
    auto *history = button(schedule, "예약·실행 이력", "serverHistory");
    auto *jobChoice = new QComboBox;
    jobChoice->setObjectName("serverJobs");
    schedule->addWidget(jobChoice);
    auto *cancel = button(schedule, "예약 취소", "serverCancelJob");
    const auto jobs = [=] {
        client->request("GET", "/jobs", {}, [=](ServerClient::Result r) {
            if (showError(r))
                return;
            jobChoice->clear();
            for (const auto &v : r.json.value("jobs").toArray()) {
                const auto o = v.toObject();
                jobChoice->addItem(o.value("state").toString() + " · " + o.value("id").toString(),
                                   o.value("id").toString());
            }
            client->request("GET", "/runs", {}, [=](ServerClient::Result runs) {
                if (!showError(runs))
                    receipt->setPlainText(
                        QJsonDocument(QJsonObject{{"jobs", r.json.value("jobs")}, {"runs", runs.json.value("runs")}})
                            .toJson());
            });
        });
    };
    connect(history, &QPushButton::clicked, this, jobs);
    connect(createJob, &QPushButton::clicked, this, [=] {
        guard([&] {
            auto data = batch();
            data["projectId"] = state->current.value("id");
            data["intervalSeconds"] = interval->value() * 60;
            data["startAt"] = start->dateTime().toSecsSinceEpoch();
            client->request("POST", "/jobs", data, [=](ServerClient::Result r) {
                if (showError(r))
                    return;
                status->setText("예약을 저장했습니다. 서버가 실행 중일 때 실행됩니다.");
                jobs();
            });
        });
    });
    connect(cancel, &QPushButton::clicked, this, [=] {
        if (jobChoice->currentData().toString().isEmpty())
            return;
        client->request("DELETE", "/jobs/" + jobChoice->currentData().toString(), {}, [=](ServerClient::Result r) {
            if (!showError(r))
                jobs();
        });
    });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, client, [client] { client->stopFollowing(); });
    prepareEditorDialog(*this);
}
} // namespace jobs
