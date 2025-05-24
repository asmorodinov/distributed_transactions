# distributed_transactions

## Описание

Реализация протоколов для геораспределённых транзакций

## Зависимости

- yatool (система сборки)
- yt/yt/core (многопоточные примитивы)
- docker (опционально, можно и без него запустить)
- docker compose (тоже опционально)

## Реализованные алгоритмы
- Двухфазная блокировка (2PL)
- Двухфазный коммит (2PC)
- Multiversion concurrency control (MVCC)
- Гибридные логические часы (HLC)
- Чтение по timestamp

## Сборка

```
./ya make -r distributed_transactions
```

Тестировалась сборка только на Ubuntu 22.04, но возможно заработает и на другой ОС

## Код
- основной код находится в папке distributed_transactions
- всё остальное - это код из yatool (из форка репозитория)
- оригинальное readme для yatool можно прочитать [тут](https://github.com/yandex/yatool)

## Запуск через docker compose

Версия без HLC
```
docker compose -f distributed_transactions/docker_configs/docker-compose.yml up --build
```

Версия с HLC
```
docker compose -f distributed_transactions/docker_configs/docker-compose-hlc.yml up --build
```

## Запуск тестов
```
docker exec tablet_stress_test ./tablet_stress_test --tablet tablet_1:8080 --tablet tablet_2:8080 --tablet tablet_3:8080 --increments 100 --keys 10 --threads 4
```

```
docker exec tablet_stress_test_transfers ./tablet_stress_test_transfers --tablet tablet_1:8080 --tablet tablet_2:8080 --tablet tablet_3:8080 --transfers 1000 --keys 30 --threads 4
```

## Настройка задержки для исходящих пакетов
```
# Пример
docker exec tablet_1 tc qdisc add dev eth0 root netem delay 5ms
```
https://medium.com/@kazushi/simulate-high-latency-network-using-docker-containerand-tc-commands-a3e503ea4307
