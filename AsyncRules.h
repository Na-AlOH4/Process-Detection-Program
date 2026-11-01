// AsyncRules.h
#pragma once
#include <string>

// 启动后台阻止任务（若已有任务在跑则忽略）
void StartSetRulesAsync();

// 启动后台恢复任务（若已有任务在跑则忽略）
void StartDeleteRulesAsync();

// 当前是否有任务在执行
bool IsRulesBusy();

// 主循环每帧调用：把后台线程产生的消息排空并交给 AddMessage
void FlushPendingMessages();

// 退出前调用，等待后台线程结束
void ShutdownRulesWorker();