#pragma once
#include "application/Document.h"
#include <QDialog>
namespace ppm {
class LevelingDecisionDialog : public QDialog {
public:
    explicit LevelingDecisionDialog(Document &, QWidget *parent = nullptr);
    ~LevelingDecisionDialog() override;

private:
    Document &document_;
    std::uint64_t subscription_ = 0;
};
} // namespace ppm
