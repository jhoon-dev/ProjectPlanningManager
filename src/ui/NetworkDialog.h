#pragma once
#include "application/Document.h"
#include "calculation/NetworkDiagram.h"
#include <QDialog>
#include <functional>
#include <map>
#include <QPointF>
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QGraphicsView;
class QGraphicsScene;
class QTableView;
class QPlainTextEdit;
namespace jobs {
void syncNetworkSelection(QWidget *, const std::string &);
class NetworkRelationsModel;
class NetworkDialog final : public QDialog {
public:
    using ScopeProvider = std::function<std::set<std::string>()>;
    NetworkDialog(Document &, std::string focus, ScopeProvider, QWidget *parent = nullptr);
    ~NetworkDialog() override;
    void reject() override;
    void setSelection(const std::string &);
    std::function<void(const std::string &, const ScheduleResult &)> taskActivated;

private:
    Document &document_;
    ScopeProvider scopeProvider_;
    std::uint64_t listener_ = 0, session_ = 0, generation_ = 0;
    std::string focus_, selected_;
    std::set<std::string> visible_;
    std::shared_ptr<const NetworkGraph> graph_;
    std::shared_ptr<CalculationControl> control_;
    NetworkWindow window_;
    QComboBox *mode_, *found_;
    QSpinBox *radius_, *limit_;
    QCheckBox *visibleOnly_, *labels_;
    QLineEdit *find_;
    QLabel *summary_;
    QPlainTextEdit *details_;
    QPushButton *refresh_, *cancel_, *jump_, *center_, *previous_, *next_;
    QGraphicsScene *scene_;
    QGraphicsView *view_;
    QTableView *relations_;
    NetworkRelationsModel *relationsModel_;
    bool windowReady_ = false, savingLayout_ = false;
    std::map<std::string, QPointF> draftPositions_;
    void saveLayout();
    void editRelations();
    void printNetwork();
    bool current() const;
    void invalidate(const QString &);
    void refresh();
    void requestWindow(std::size_t offset = 0);
    void render();
    void select(const std::string &);
    void jump();
    void find();
    void summary();
};
} // namespace jobs
