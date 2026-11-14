# Статус безопасности

Device / hardware revision: UNKNOWN (устройство не подключалось)
Current firmware / bootloader: UNKNOWN
Source / SDK / protocol revision: ZARYA 0.1.0; целевой SDK fm1_open @ 4c7496da (V14); протокол редактора v1
Proposed operation and exact target: нет
Verified backup coverage: NOT VERIFIED
Verified recovery: NOT VERIFIED
Build (pi32v2 device image): NOT RUN — компилятор не удалось запустить в среде разработки
Firmware core tests (host, ASan+UBSan): PASS
Web tests / build / браузерный прогон: PASS (симулятор)
Hardware testing: NOT RUN

## Decision: NO-GO: DO NOT FLASH

Образа прошивки не существует. Адаптер `device/zr_device.c` — некомпилированный черновик.
До первой прошивки нужны: сборка официальным тулчейном, проверка карты памяти и размера,
бэкап проектов/сэмплов/голосов в официальном workbench, проверенный на ВАШЕЙ ревизии
способ возврата на сток (workbench умеет «return to original V15») и аварийного
восстановления, и отдельное явное согласие на конкретный образ с его SHA-256.
