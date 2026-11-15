#!/bin/bash
# User installation; the source package may be on a noexec USB drive.
set -Eeuo pipefail
umask 077

fail() { printf 'Ошибка установки: %s\n' "$*" >&2; exit 1; }
script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
package_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
[[ ${HOME:-} = /* && $HOME != *$'\n'* && $HOME != *$'\r'* ]] || fail 'некорректный HOME'
for tool in sha256sum mktemp cp mv chmod mkdir readlink; do
    command -v "$tool" >/dev/null || fail "не найдена команда $tool"
done
for name in AstraMail Run-AstraMail.sh Verify-Package.sh version.txt lib/libsodium.so.18 package-manifest.json package-checksums.sha256; do
    [[ -f "$package_dir/$name" ]] || fail "в пакете отсутствует $name"
done

# Copy only declared payload, excluding runtime files beside a portable copy.
declare -a files=()
while IFS= read -r line || [[ -n $line ]]; do
    [[ $line =~ ^[[:xdigit:]]{64}\ \ (.+)$ ]] || fail 'неверный формат контрольных сумм'
    relative=${BASH_REMATCH[1]}
    case "$relative" in
        /*|*\\*|*$'\r'*|.|..|./*|../*|*/./*|*/../*|*/.|*/..|package-checksums.sha256)
            fail 'недопустимый путь в контрольных суммах' ;;
    esac
    resolved=$(readlink -f -- "$package_dir/$relative")
    [[ $resolved = "$package_dir/"* && -f $resolved && ! -L "$package_dir/$relative" ]] || fail "недопустимый файл $relative"
    files+=("$relative")
done < "$package_dir/package-checksums.sha256"
(cd -- "$package_dir" && sha256sum -c -- package-checksums.sha256)

install_parent="$HOME/.local/opt"
target="$install_parent/AstraMail"
bin_dir="$HOME/.local/bin"
launcher="$bin_dir/AstraMail"
menu_dir="$HOME/.local/share/applications"
menu_entry="$menu_dir/AstraMail.desktop"
desktop_dir=''
# Astra 1.6's xdg-user-dir uses unquoted paths and eval. Read its data file
# without sourcing shell code, including when HOME contains spaces/metacharacters.
decode_xdg_path() {
    local value=$1 result='' char next
    while [[ -n $value ]]; do
        char=${value:0:1}; value=${value:1}
        if [[ $char = \\ && -n $value ]]; then
            next=${value:0:1}
            case "$next" in '\'|'"'|'$'|'`') char=$next; value=${value:1} ;; esac
        fi
        result+=$char
    done
    printf '%s' "$result"
}
dirs_config="${XDG_CONFIG_HOME:-$HOME/.config}/user-dirs.dirs"
if [[ -f $dirs_config ]]; then
    while IFS= read -r line || [[ -n $line ]]; do
        if [[ $line =~ ^[[:space:]]*XDG_DESKTOP_DIR=\"(.*)\"[[:space:]]*$ ]]; then
            value=${BASH_REMATCH[1]}
            case "$value" in
                '$HOME') desktop_dir=$HOME ;;
                '$HOME/'*) desktop_dir="$HOME/$(decode_xdg_path "${value#\$HOME/}")" ;;
                /*) desktop_dir=$(decode_xdg_path "$value") ;;
            esac
        fi
    done < "$dirs_config"
fi
if [[ -z $desktop_dir && $HOME =~ ^/[[:alnum:]_./-]+$ ]] && command -v xdg-user-dir >/dev/null; then
    desktop_dir=$(xdg-user-dir DESKTOP 2>/dev/null) || desktop_dir=''
fi
[[ -n $desktop_dir ]] || desktop_dir="$HOME/Desktop"
[[ $desktop_dir = /* && $desktop_dir != *$'\n'* && $desktop_dir != *$'\r'* ]] || fail 'некорректный каталог рабочего стола'
desktop_entry=''
# xdg-user-dir returns HOME when desktop icons are disabled.
if [[ $desktop_dir != "$HOME" ]]; then desktop_entry="$desktop_dir/AstraMail.desktop"; fi
[[ ! -L $target && ( ! -e $target || -d $target ) ]] || fail "недопустимый каталог установки: $target"
for destination in "$launcher" "$menu_entry" "${desktop_entry:-$menu_entry}"; do
    [[ ! -L $destination && ( ! -e $destination || -f $destination ) ]] || fail "недопустимый файл назначения: $destination"
done
mkdir -p -- "$install_parent" "$bin_dir" "$menu_dir"
if [[ -n $desktop_entry ]]; then mkdir -p -- "$desktop_dir"; fi
transaction=$(mktemp -d "$install_parent/.AstraMail.install.XXXXXXXX")
mkdir -- "$transaction/app" "$transaction/previous"
app_changed=0
launcher_changed=0
menu_changed=0
desktop_changed=0
committed=0

restore_file() {
    local name=$1 destination=$2
    if [[ -f "$transaction/previous/$name" ]]; then
        cp -p -- "$transaction/previous/$name" "$destination"
    else
        rm -f -- "$destination"
    fi
}
finish() {
    local status=$?
    trap - EXIT
    if [[ $committed = 0 ]]; then
        set +e
        [[ $desktop_changed = 0 ]] || restore_file desktop "$desktop_entry"
        [[ $menu_changed = 0 ]] || restore_file menu "$menu_entry"
        [[ $launcher_changed = 0 ]] || restore_file launcher "$launcher"
        if [[ $app_changed = 1 ]]; then mv -- "$target" "$transaction/failed-app"; fi
        if [[ -d "$transaction/previous-app" ]]; then mv -- "$transaction/previous-app" "$target"; fi
        printf 'Установка не завершена. Материалы и прежняя копия: %s\n' "$transaction" >&2
        [[ $status != 0 ]] || status=1
    fi
    exit "$status"
}
trap finish EXIT
trap 'printf "Ошибка установки в строке %s.\n" "$LINENO" >&2' ERR
trap 'exit 130' INT
trap 'exit 143' TERM

for relative in "${files[@]}"; do
    mkdir -p -- "$transaction/app/$(dirname -- "$relative")"
    cp -- "$package_dir/$relative" "$transaction/app/$relative"
done
cp -- "$package_dir/package-checksums.sha256" "$transaction/app/"
# Execute only the new LOCAL copy, never an ELF from USB.
sh "$transaction/app/Verify-Package.sh"
version=$(cat "$transaction/app/version.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'неверный номер версии'

# Preserve only application-owned files beside the previous executable.
# HOME-based mail, profiles and secrets keep their existing locations.
old_config_dir="$target"
if [[ ! -d $old_config_dir && -f $launcher ]]; then old_config_dir="$bin_dir"; fi
for name in subject_rules.conf imap_folders.conf attachment_groups.conf outgoing_subject_rules.conf idle_settings.conf network_settings.conf; do
    if [[ -f "$old_config_dir/$name" ]]; then
        [[ ! -L "$old_config_dir/$name" ]] || fail "конфигурация является ссылкой: $name"
        cp -p -- "$old_config_dir/$name" "$transaction/app/$name"
    fi
done
cat > "$transaction/launcher" <<'EOF'
#!/bin/sh
set -eu
bin_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec /bin/sh "$bin_dir/../opt/AstraMail/Run-AstraMail.sh" "$@"
EOF
chmod 755 -- "$transaction/launcher"

# Desktop entries have both key-file escaping and Exec argument escaping.
desktop_exec=$launcher
desktop_exec=${desktop_exec//\\/\\\\}
desktop_exec=${desktop_exec//\"/\\\"}
desktop_exec=${desktop_exec//\$/\\\$}
desktop_exec=${desktop_exec//\`/\\\`}
desktop_exec=${desktop_exec//%/%%}
desktop_exec=${desktop_exec//\\/\\\\}
printf '[Desktop Entry]\nType=Application\nName=AstraMail\nExec=/bin/sh "%s"\nTerminal=true\nIcon=utilities-terminal\nCategories=Network;Email;\n' "$desktop_exec" > "$transaction/desktop"
chmod 755 -- "$transaction/desktop"
for name in launcher menu desktop; do
    case "$name" in launcher) destination=$launcher ;; menu) destination=$menu_entry ;; desktop) destination=$desktop_entry ;; esac
    if [[ -n $destination && -f $destination ]]; then cp -p -- "$destination" "$transaction/previous/$name"; fi
done

if [[ -d $target ]]; then mv -- "$target" "$transaction/previous-app"; fi
mv -- "$transaction/app" "$target"
app_changed=1
launcher_changed=1
cp -- "$transaction/launcher" "$launcher"
chmod 755 -- "$launcher"
menu_changed=1
cp -- "$transaction/desktop" "$menu_entry"
chmod 755 -- "$menu_entry"
if [[ -n $desktop_entry ]]; then
    desktop_changed=1
    cp -- "$transaction/desktop" "$desktop_entry"
    chmod 755 -- "$desktop_entry"
fi
[[ $("$launcher" --version) = "$version" ]] || fail 'установленная версия не прошла проверку'
committed=1
printf '\nAstraMail %s установлен: %s\nКоманда запуска: %s\nЯрлык меню: %s\n' "$version" "$target" "$launcher" "$menu_entry"
if [[ -n $desktop_entry ]]; then printf 'Ярлык рабочего стола: %s\n' "$desktop_entry"; fi
printf 'Предыдущие файлы и материалы установки сохранены: %s\nГотово.\n' "$transaction"
