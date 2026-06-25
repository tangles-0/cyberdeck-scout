#!/usr/bin/env bash
set -euo pipefail

# Lightweight desktop setup for BIGTREETECH CB2 Debian/Armbian images.
# Installs LXDE/Openbox + LightDM + Chromium without the full desktop task.

AUTOLOGIN_USER="${1:-${SUDO_USER:-tangles}}"

if [[ "${EUID}" -ne 0 ]]; then
	echo "Run as root, for example: sudo $0 [autologin-user]" >&2
	exit 1
fi

if ! id "${AUTOLOGIN_USER}" >/dev/null 2>&1; then
	echo "User does not exist: ${AUTOLOGIN_USER}" >&2
	exit 1
fi

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
	xserver-xorg \
	lightdm \
	lightdm-gtk-greeter \
	lxde-core \
	lxterminal \
	dbus-x11 \
	policykit-1 \
	x11-xserver-utils \
	fonts-dejavu-core \
	adwaita-icon-theme \
	chromium \
	chromium-sandbox \
	xdg-utils

install -d -m 0755 /etc/lightdm/lightdm.conf.d
cat >/etc/lightdm/lightdm.conf.d/50-cb2-lxde.conf <<EOF
[Seat:*]
autologin-user=${AUTOLOGIN_USER}
autologin-session=LXDE
user-session=LXDE
greeter-session=lightdm-gtk-greeter
EOF

user_home="$(getent passwd "${AUTOLOGIN_USER}" | cut -d: -f6)"
install -d -o "${AUTOLOGIN_USER}" -g "${AUTOLOGIN_USER}" \
	"${user_home}/.config/lxsession/LXDE" \
	"${user_home}/.config/lxpanel/LXDE/panels"

cat >"${user_home}/.config/lxsession/LXDE/autostart" <<'EOF'
@lxpanel --profile LXDE
@pcmanfm --desktop --profile LXDE
@xset s off
@xset -dpms
@xset s noblank
EOF
chown "${AUTOLOGIN_USER}:${AUTOLOGIN_USER}" "${user_home}/.config/lxsession/LXDE/autostart"
chmod 0644 "${user_home}/.config/lxsession/LXDE/autostart"

panel_src="/etc/xdg/lxpanel/LXDE/panels/panel"
panel_dst="${user_home}/.config/lxpanel/LXDE/panels/panel"
if [[ ! -f "${panel_dst}" ]]; then
	cp "${panel_src}" "${panel_dst}"
fi
sed -i 's/id=lxde-x-www-browser.desktop/id=chromium.desktop/g' "${panel_dst}"
chown "${AUTOLOGIN_USER}:${AUTOLOGIN_USER}" "${panel_dst}"
chmod 0644 "${panel_dst}"

if command -v update-alternatives >/dev/null && command -v chromium >/dev/null; then
	update-alternatives --set x-www-browser /usr/bin/chromium || true
	update-alternatives --set gnome-www-browser /usr/bin/chromium || true
fi

install -d -m 0755 /etc/chromium.d
cat >/etc/chromium.d/cb2-low-memory <<'EOF'
# Keep Chromium a little quieter on 2GB CB2 systems.
CHROMIUM_FLAGS="${CHROMIUM_FLAGS:-} --disable-sync --disable-background-networking --process-per-site"
EOF

systemctl set-default graphical.target
systemctl enable lightdm

if systemctl is-active --quiet lightdm; then
	systemctl restart lightdm
else
	systemctl start lightdm
fi

echo "CB2 lightweight desktop installed."
echo "Autologin user: ${AUTOLOGIN_USER}"
echo "Session: LXDE via LightDM"
echo "Browser launcher: Chromium"
