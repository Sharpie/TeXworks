/*
  This is part of TeXworks, an environment for working with TeX documents
  Copyright (C) 2007-2011  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For links to further information, or to contact the author,
  see <http://www.tug.org/texworks/>.
*/
#include "ConfigurableApp.h"
#include "TWScriptAPI.h"

#include "TWCoffeeScriptPlugin.h"

#include <QTextStream>
#include <QtPlugin>
#include <QMetaObject>
#include <QStringList>
#include <QSettings>
#include <QtScript>
#if QT_VERSION >= 0x040500
#include <QtScriptTools>
#endif


static
QVariant convertValue(const QScriptValue& value)
{
  if (value.isArray()) {
    QVariantList lst;
    int len = value.property("length").toUInt32();
    for (int i = 0; i < len; ++i) {
      QScriptValue p = value.property(i);
      lst.append(convertValue(p));
    }
    return lst;
  }
  else
    return value.toVariant();
}


TWScript* TWCoffeeScriptPlugin::newScript(const QString& fileName)
{
  return new CoffeeScript(this, fileName);
}

Q_EXPORT_PLUGIN2(TWCoffeeScriptPlugin, TWCoffeeScriptPlugin)


bool CoffeeScript::execute(TWScriptAPI *tw) const
{
  QFile scriptFile(m_Filename), coffeeScriptSrc(":/TWCoffeeScriptPlugin/coffee-script.js");

  coffeeScriptSrc.open(QIODevice::ReadOnly);
  QString coffeeScript = QString::fromUtf8(coffeeScriptSrc.readAll());
  coffeeScriptSrc.close();

  if (!scriptFile.open(QIODevice::ReadOnly)) {
    // handle error
    return false;
  }
  QTextStream stream(&scriptFile);
  stream.setCodec(m_Codec);
  QString contents = stream.readAll();
  scriptFile.close();

  QScriptEngine engine;
  QScriptValue twObject = engine.newQObject(tw);
  engine.globalObject().setProperty("TW", twObject);

  engine.evaluate(coffeeScript, "coffee-script.js");
  engine.globalObject().setProperty("coffee_source", contents);

  QScriptValue compiledScript;
  QScriptValue val;

#if QT_VERSION >= 0x040500
  // FIXME:
  // Accessing QSETTINGS_OBJECT causes a segfault---but only when compiled
  // under O2 optimization or higher.
  //
  // Perhaps related to the fact that this code is in one dylib and the code
  // accessed by QSETTINGS_OBJECT is in another? WHY?!
  //QSETTINGS_OBJECT(settings);
  //if (settings.value("scriptDebugger", false).toBool()) {
    QScriptEngineDebugger debugger;
    debugger.attachTo(&engine);
  //}
#endif

  compiledScript = engine.evaluate("this.CoffeeScript.compile(this.coffee_source);", m_Filename);
  val = engine.evaluate(compiledScript.toString(), m_Filename);

  if (engine.hasUncaughtException()) {
    tw->SetResult(engine.uncaughtException().toString());
    return false;
  }
  else {
    if (!val.isUndefined()) {
      tw->SetResult(convertValue(val));
    }
    return true;
  }
}

