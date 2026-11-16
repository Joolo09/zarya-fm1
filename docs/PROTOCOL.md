# Протокол редактора ZARYA v1

Кадр: `F0 7D 5A 52 <cmd> <seq> <payload, 7-бит> F7`, не длиннее 128 байт.
`7D` — некоммерческий Manufacturer ID. Упаковка: группы по 7 байт → байт старших битов + 7 байт.
Ответ несёт `seq` запроса. Всё, что не распознано, игнорируется или получает `ERROR`.

| cmd | Направление | Payload | Ответ | Класс |
|---|---|---|---|---|
| 01 HELLO | → | — | 41: proto, fw×3, schema, tracks, voices, steps, scenes, caps×2, storage, patchParams, globals, hardware | чтение |
| 02 GET_BLOB | → | kind, a, b | 42 ×N: kind, a, b, off×2, total×2, ≤96 байт | чтение |
| 03 PUT_BLOB | → | kind, a, b, off×2, total×2, ≤96 байт | 43: kind, a, b, status, got×2, done | временная правка |
| 04 SET_PARAM | → | scope(0 patch/1 global), track, id, value | — | временная правка |
| 05 TRANSPORT | → | 0/1 | — | временная |
| 06 NOTE | → | track, note, vel | — | временная |
| 07 GET_STATUS | → | — | 47: …, degree, pureRoot, duet, captureSeq, captureCount | чтение |
| 08 GENERATE | → | track | 48: track, scene | временная правка |
| 09 SELECT | → | track, scene (8 = не менять) | — | временная |
| 0A LIVE | → | op, arg: 0 = ступень аккорда 0..6; 1 = CAPTURE дорожки | — | временная правка |
| 4A EVT_PARAM | ← | scope, track, id, value | | |
| 4B EVT_PATCH | ← | track | | |
| 4F ERROR | ← | cmd, code, detail | | |

Блоб: `'Z' 'R' kind schema len×2 crc16×2` + данные. kind: 1 PATCH (100 байт, a=трек),
2 PATTERN (260, a=сцена, b=трек), 3 GLOBALS (64). CRC-16/CCITT-FALSE.

Capabilities: 1 PARAM, 2 BLOB_GET, 4 BLOB_PUT, 8 TRANSPORT, 16 GENERATE,
128 LIVE, 32 PERSIST (не реализовано), 64 FLASH (не будет реализовано в этом протоколе).
**Команд записи во флеш и обновления прошивки в протоколе нет.**
