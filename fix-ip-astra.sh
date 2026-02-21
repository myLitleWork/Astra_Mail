#!/bin/bash
# fix-ip-astra.sh — зафиксировать текущий IP в Astra Linux

# Получаем активное соединение: устройство и имя (поле NAME, не CONNECTION)
LINE=$(nmcli -t -f DEVICE,NAME con show --active 2>/dev/null | grep -v "^lo:" | head -1)
IFACE="${LINE%%:*}"
CONN="${LINE#*:}"

if [ -z "$IFACE" ] || [ -z "$CONN" ]; then
  echo "Не удалось определить активное соединение. Запустите: nmcli connection show"
  exit 1
fi

CURRENT_IP=$(ip -4 addr show "$IFACE" 2>/dev/null | grep -oP 'inet \K[\d.]+/[\d]+')
GATEWAY=$(ip route 2>/dev/null | grep default | awk '{print $3}')

if [ -z "$CURRENT_IP" ]; then
  echo "Не удалось определить IP на $IFACE"
  exit 1
fi

echo "Интерфейс: $IFACE"
echo "Соединение: $CONN"
echo "IP: $CURRENT_IP"
echo "Шлюз: $GATEWAY"

nmcli con mod "$CONN" ipv4.addresses "$CURRENT_IP"
nmcli con mod "$CONN" ipv4.gateway "$GATEWAY"
nmcli con mod "$CONN" ipv4.method manual
nmcli con down "$CONN" ; nmcli con up "$CONN"

echo "Готово. IP зафиксирован."
