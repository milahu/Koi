#include "konsole.h"

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QIODevice>
#include <QXmlStreamReader>

QStringList Konsole::getThemes() {
  QDir konsoleProfileLocalDir(QDir::homePath() + "/.local/share/konsole");
  QDir konsoleProfileSystemDir("/usr/share/konsole");
  QStringList konsoleProfiles;

  if (konsoleProfileLocalDir.exists()) {
    konsoleProfileLocalDir.setNameFilters(QStringList() << "*.profile");
    konsoleProfileLocalDir.setFilter(QDir::Files);
    konsoleProfileLocalDir.setSorting(QDir::Name);
    QList<QFileInfo> colorSchemesLocal = konsoleProfileLocalDir.entryInfoList();

    QStringList konsoleProfilesLocalNames;
    for (QFileInfo fileInfo : colorSchemesLocal) {
      konsoleProfilesLocalNames.append(fileInfo.baseName());
    }
    konsoleProfiles += konsoleProfilesLocalNames;
  }
  if (konsoleProfileSystemDir.exists()) {
    konsoleProfileSystemDir.setNameFilters(QStringList() << "*.profile");
    konsoleProfileSystemDir.setFilter(QDir::Files);
    konsoleProfileSystemDir.setSorting(QDir::Name);
    QList<QFileInfo> colorSchemesLocal =
        konsoleProfileSystemDir.entryInfoList();

    QStringList konsoleProfilesSystemNames;
    for (QFileInfo fileInfo : colorSchemesLocal) {
      konsoleProfilesSystemNames.append(fileInfo.baseName());
    }
    konsoleProfiles += konsoleProfilesSystemNames;
  }

  konsoleProfiles.removeDuplicates();
  konsoleProfiles.sort();
  return konsoleProfiles;
}

void Konsole::setTheme(QString theme) {
  // Update the config file. Should be like this...
  // konsoleSettings->setValue("Desktop Entry/DefaultProfile", theme +
  // ".profile");
  // However, QSettings does ANSI escaping which we don't like.
  // So, we need to do by hand.
  QFile konsoleConfig(QDir::homePath() + "/.config/konsolerc");

  if (konsoleConfig.exists()) {
    // First read the content.
    konsoleConfig.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream reader(&konsoleConfig);
    QStringList configContent;
    while (!reader.atEnd()) {
      QString configLine = reader.readLine();
      // Do line by line so that we can catch this line and update it.
      if (configLine.startsWith("DefaultProfile="))
        configLine = QString("DefaultProfile=" + theme + ".profile");
      configContent << configLine;
    }

    konsoleConfig.close();

    // Now, write the updated content.
    konsoleConfig.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream writer(&konsoleConfig);
    for (QString line : configContent) {
      writer << line;
      writer << "\n";
    }
    konsoleConfig.close();
  }

  // Now live update everything else via DBus.
  // This will also re-update the config file, which is absolutely fine.

  // Build up the list of things to refresh
  QStringList servicesToRefresh;
  servicesToRefresh.append("org.kde.konsole");
  servicesToRefresh.append("org.kde.yakuake");

  // Find the existing Konsole and Dolphin processes via session DBus
  QDBusConnection sessionBus = QDBusConnection::sessionBus();
  QStringList registeredServices =
      sessionBus.interface()->registeredServiceNames();
  for (QString registeredService : registeredServices) {
    // We care only of Konsole processes, which do advertise themselves on the
    // session bus.
    if (registeredService.startsWith("org.kde.konsole") ||
        registeredService.startsWith("org.kde.dolphin"))
      servicesToRefresh.append(registeredService);
  }

  for (QString serviceToRefresh : servicesToRefresh) {
    updateKonsoleProfile(serviceToRefresh, theme, sessionBus);
  }
}

/**
Heavy work for updating a Konsole tab.
Identify all sessions for Konsole and call the correct refresh on all of them.
 */
void Konsole::updateKonsoleProfile(QString serviceName, QString theme,
                                   QDBusConnection &sessionBus) {
  QDBusInterface sessionInterface = QDBusInterface(
      serviceName, QString("/Sessions"),
      QString("org.freedesktop.DBus.Introspectable"), sessionBus);

  QDBusReply<QString> sessionsReply =
      sessionInterface.call(QString("Introspect"));

  // Found no better way than parsing the XML...
  // So we do that.
  QXmlStreamReader replyReader(sessionsReply);
  QStringList sessionPaths;

  while (!replyReader.atEnd()) {
    // The nodes we are interested are:
    // <node name="1" />
    // Which are the /Sessions/NAME paths.
    if (replyReader.isStartElement() && replyReader.name() == "node" &&
        replyReader.attributes().hasAttribute("name")) {
      QString sessionPath;
      sessionPath.append("/Sessions/");
      sessionPath.append(replyReader.attributes().value("name"));
      sessionPaths.append(sessionPath);
    }
    replyReader.readNext();
  }

  for (QString sessionPath : sessionPaths) {
    // std::cout << "Refreshing: " << serviceName.toStdString() << "@"
    //           << sessionPath.toStdString() << std::endl;
    QDBusMessage refreshCall = QDBusMessage::createMethodCall(
        serviceName, sessionPath, "org.kde.konsole.Session", "setProfile");
    refreshCall.setArguments({theme});
    sessionBus.call(refreshCall);
  }
}