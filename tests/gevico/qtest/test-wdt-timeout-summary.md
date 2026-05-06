# G233 WDT 实现总结

## 测试覆盖的 7 个功能

| 测试 | 验证内容 |
|------|---------|
| config | 寄存器 LOAD/CTRL 读写正常 |
| countdown | 时钟步进后计数器递减 |
| feed | 写 KEY=0x5A5A5A5A 重载计数器 |
| timeout_flag | 计数器到 0 置 SR.TIMEOUT |
| timeout_clear | 写 SR 清零 (W1C) |
| lock | 写 KEY=0x1ACCE551 锁定后阻止修改 |
| interrupt | 超时 + INTEN 时 PLIC IRQ 4 挂起 |

## 新增/修改文件

| 操作 | 文件 | 说明 |
|------|------|------|
| **新增** | `include/hw/watchdog/g233_wdt.h` | WDT 类型声明、寄存器宏、状态结构体 |
| **新增** | `hw/watchdog/g233_wdt.c` | WDT 设备实现（210 行） |
| 修改 | `hw/watchdog/meson.build` | 添加 CONFIG_G233_WDT 编译规则 |
| 修改 | `hw/watchdog/Kconfig` | 添加 G233_WDT 配置项 |
| 修改 | `hw/riscv/Kconfig` | GEVICO_G233 select G233_WDT |
| 修改 | `include/hw/riscv/g233.h` | 添加 VIRT_WDT 枚举、WDT_IRQ=4 |
| 修改 | `hw/riscv/g233.c` | include、内存映射、WDT 实例化 |

## 核心设计要点

- **计数器速率**：1000 ns/tick（每微秒减 1）
- **递减引擎**：每次寄存器访问时计算虚拟时间流逝，换算成 tick
- **超时**：计数器到 0 置 SR.TIMEOUT，若 INTEN 使能则触发中断
- **喂狗**：KEY=0x5A5A5A5A 重载计数器、清除超时标志
- **锁定**：KEY=0x1ACCE551 设置 locked，后续除 KEY 外写入被忽略
- **W1C**：SR 寄存器写入的位被清除

## 数据流

```
qtest 读写 → MMIO 回调 (g233_wdt_read/write)
  → g233_wdt_update_counter 计算流逝 tick
    → 递减 s->val / 置超时标志
      → g233_wdt_update_irq 输出到 PLIC
```
