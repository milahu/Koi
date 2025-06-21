#pragma once

#include "src/headers/plugin.h"

class Konsole : DbusPlugin {
public:
  void setTheme(QString theme) override;
  QStringList getThemes() override;

private:
  void updateKonsoleProfile(QString serviceId, QString theme,
                            QDBusConnection &sessionBus);
};