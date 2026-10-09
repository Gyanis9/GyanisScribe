#include "App/Application.h"

#include <QApplication>

/**
 * @brief 进程入口
 * @details 这里暂时没有窗口：界面在 TrayController 与 MainWindow 落地那一批进来，本批次只保证
 *          「可执行文件起得来、日志写得出、测试跑得了」。直接跑它会安静地等在事件循环里，属预期。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 事件循环退出码
 */
int main(int argc, char *argv[])
{
    Application application(argc, argv);
    return QApplication::exec();
}
