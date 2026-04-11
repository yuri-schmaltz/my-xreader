#!/bin/bash
set -e

VERSION="5.0.0"
echo "============================================"
echo "    XREADER RELEASE MANAGER ($VERSION)"
echo "============================================"

# Limpar artefatos passados
rm -f xreader_*.deb xreader-*.rpm Xreader-*.AppImage
rm -rf AppDir

echo ">> 1. COMPILANDO PACOTE *.DEB (dpkg-buildpackage)"
# Ignorando verificações GPG para release unificada
dpkg-buildpackage -us -uc -b

# Localiza arquivo DEB base gerado pelo debhelper
DEB_FILE=$(ls ../xreader_${VERSION}_amd64.deb | head -n 1)
cp $DEB_FILE ./
DEB_LOCAL_FILE="xreader_${VERSION}_amd64.deb"
echo "=> DEB gerado: $DEB_LOCAL_FILE"

echo ">> 2. CONVERTENDO PARA *.RPM (alien)"
sudo alien -r -c $DEB_LOCAL_FILE
RPM_FILE=$(ls xreader-${VERSION}-*.x86_64.rpm | head -n 1)
echo "=> RPM gerado: $RPM_FILE"

echo ">> 3. PREPARANDO PACOTE *.AppImage"
mkdir -p AppDir/usr
# Faremos o install nativo na isolação da AppDir
meson setup builddir-appimage --prefix=/usr
ninja -C builddir-appimage install DESTDIR=$(pwd)/AppDir

# A instalação já clona o desktop em /usr/share/applications/ e /usr/share/icons/

# Baixar linuxdeploy
if [ ! -f "linuxdeploy-x86_64.AppImage" ]; then
    wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi
if [ ! -f "linuxdeploy-plugin-gtk.sh" ]; then
    wget -q https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
    chmod +x linuxdeploy-plugin-gtk.sh
fi

export OUTPUT="Xreader-${VERSION}-x86_64.AppImage"
export LINUXDEPLOY_PLUGIN_MODE="gtk"
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin gtk --output appimage
APPIMAGE_FILE=$OUTPUT
echo "=> APPIMAGE gerado: $APPIMAGE_FILE"

echo ">> 4. PREPARANDO LANÇAMENTO NO GITHUB RELEASES (gh)"
git add meson.build debian/changelog
git commit -m "chore: release version ${VERSION} with GTK4 Multi-Tab" || true
git tag "v$VERSION" || true
git push origin master --tags || true

echo "=> Publicando no Github..."
# Apenas os 3 arquivos
gh release create "v$VERSION" $DEB_LOCAL_FILE $RPM_FILE $APPIMAGE_FILE -t "Release $VERSION: GTK4 Multi-Tab" -n "A arquitetura Multi-Aba para GTK4 finalizada nativamente. Esse release não contém bibliotecas desnecessárias isoladas, empacotando unicamente a engine em seus builds estáticos."
echo "✅ PUBLICAÇÃO CONCLUÍDA!"
