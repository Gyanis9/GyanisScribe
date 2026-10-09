#include <QApplication>
#include <QPushButton>

int main(int argc, char *argv[]) {
  // 局部变量按规范用完整单词，不写单字母名
  QApplication application(argc, argv);
  QPushButton button("Hello world!", nullptr);
  button.resize(200, 100);
  button.show();
  return QApplication::exec();
}
