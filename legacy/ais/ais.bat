@echo off

set CP=.
set CP=%CP%;classes
set CP=%CP%;www/WEB-INF/lib/ais.jar
set CP=%CP%;www/WEB-INF/lib/log4j.jar
set CP=%CP%;www/WEB-INF/lib/commons-logging.jar
set CP=%CP%;www/WEB-INF/lib/lucene-core-2.4.1.jar


java -classpath "%CP%;%CLASSPATH%" org.is.Main %1 %2 %3 %4 %5 %6 %7 %8 %9

