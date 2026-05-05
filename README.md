# AIS

This repository preserves the final public SourceForge snapshot of the old AIS project:

https://sourceforge.net/projects/ais/

The original project is preserved here for context, specification recovery,
screenshots, and open-source continuity. The implementation will be re-engineered
rather than history-preserved.

## Original SourceForge Material

- Original source snapshot: `legacy/`
- SourceForge page archive: `doc/original/sourceforge-archive/`
- Screenshots: `doc/original/screenshots/`
- Downloaded release artifacts: `doc/original/downloads/`

## Re-engineering Direction

The new implementation is expected to follow the same lightweight technology
direction used in the KUL project:

- Java backend
- Servlet-based REST endpoints
- MySQL
- Static HTML/CSS/JavaScript frontend
- Plain SQL DAO classes
- Apache Ant build
- Embedded Jetty for local development
- Tomcat deployment target

## License

The project is open source in continuity with the original SourceForge project.
See `LICENSE`.
