#include <QCoreApplication>
#include <QtGlobal>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QRegExp>
#include <msgpack.h>
#include <stdio.h>
#include <unistd.h>
#include "function.h"
#include "util.h"

#define NL "\n"
#define TAB "\t"
#define COMMA ", "
#define SEMICOLON ";"
#define QUOT "\""

/**
 * Convert data type names from Neovim API types into
 * our own types. For simple types this is done using
 * the typedefs in function.h - this function handles
 * ArrayOf(...) constructs - all other types are left
 * unchanged
 */
QString Type(QString type)
{
	type = type.trimmed();

	// ArrayOf(String)
	QRegExp array_template("ArrayOf\\(\\s*(\\w+)\\s*\\)");
	// ArrayOf(Integer, 2)
	QRegExp array_int2("ArrayOf\\(\\s*Integer,\\s*2\\s*\\)");

	if (type.contains(array_template)) {
		//return QString("QList<%1>").arg(array_template.cap(1));
		return QString("%1Array").arg(array_template.cap(1));
	} else if (type.contains(array_int2)){
		// TODO: we could generalise this to fixed size any type arrays
		// but this seems to be the only right now
		return "Position";
	} else if (type.contains('(')) {
		qFatal(QString("Found unsupported data type %1").arg(type).toLocal8Bit());
	}
	return type;
}

bool generate_function_enum(const QList<NeovimQt::Function> &ftable, QDir& dst)
{
	QFile f( dst.filePath(QLatin1String("function_enum.h")) );
	if ( !f.open(QIODevice::WriteOnly) ) {
		printf("Unable to open function_enum.h for writing\n");
		return false;
	}
	QTextStream out(&f);

	out << "// Auto generated " << QDateTime::currentDateTime().toString() << NL;
	out << "enum FunctionId {\n";
	foreach(NeovimQt::Function f, ftable) {
		QString l = "\tNEOVIM_FN_%1,\n";
		out << l.arg( QString(f.name).toUpper() );
	}
	out <<	"\tNEOVIM_FN_NULL\n};";
	return true;
}

typedef QPair<QString,QString> Param;
bool generate_function_static(const QList<NeovimQt::Function> &ftable, QDir& dst)
{
	QFile f( dst.filePath(QLatin1String("function_static.cpp")) );
	if ( !f.open(QIODevice::WriteOnly) ) {
		printf("Unable to open function_static.cpp for writing\n");
		return false;
	}
	QTextStream out(&f);

	out << "// Auto generated " << QDateTime::currentDateTime().toString() << NL;
	out << "const QList<Function> Function::knownFunctions = QList<Function>()\n";
	foreach(NeovimQt::Function f, ftable) {
		out << QString("<< Function(\"%1\", \"%2\",\n").arg(f.return_type, f.name);
		out << "\tQList<QString>()\n";
		foreach(Param p, f.parameters) {
			out << QString("\t\t<< QString(\"%1\")\n").arg(p.first);
		}
		out << QString("\t, %1)\n").arg(f.can_fail);
	}
	out << "\t;";
	return true;
}

bool generate_neovim_h(const QList<NeovimQt::Function> &ftable, QDir& dst)
{
	QFile f( dst.filePath(QLatin1String("neovim.h")) );
	if ( !f.open(QIODevice::WriteOnly) ) {
		printf("Unable to open neovim.h for writing\n");
		return false;
	}
	QTextStream out(&f);

	out << "// Auto generated " << QDateTime::currentDateTime().toString() << NL;
	out << "#ifndef NEOVIM_QT_NEOVIMOBJ\n";
	out << "#define NEOVIM_QT_NEOVIMOBJ\n";
	out << "#include \"function.h\"\n";
	out << "#include <msgpack.h>\n";
	out << "namespace NeovimQt {\n";
	out << "class NeovimConnector;\n";
	out << "class Neovim: public QObject\n";
	out << "{\n\n";
	out << "\tQ_OBJECT\n";
	out << "public:\n";
	out << "\tNeovim(NeovimConnector *);\n";
	out << "protected slots:\n";
	out << "\tvoid handleResponse(uint32_t id, Function::FunctionId fun, const msgpack_object&);\n";
	out << "\tvoid handleResponseError(uint32_t id, Function::FunctionId fun, const QString& msg, const msgpack_object&);\n";
	out << "signals:\n";
	out << "\tvoid error(const QString& errmsg);\n";
	out << "private:\n";
	out << "\tNeovimConnector *m_c;\n";

	out << "public slots:\n";
	foreach(NeovimQt::Function f, ftable) {
		out << QString("\tvoid %1(").arg(f.name);
		for (int i=0; i<f.parameters.size(); i++) {
			if (i) {
				out << COMMA;
			}
			out << QString("%1 %2").arg(Type(f.parameters.at(i).first), f.parameters.at(i).second);
		}
		out << ");\n";
	}
	out << "\nsignals:\n";
	foreach(NeovimQt::Function f, ftable) {
		out << QString("\tvoid %1(%2);\n").arg("on_" + f.name, Type(f.return_type));
	}

	out << "};\n";
	out << "} // namespace\n";
	out << "#endif\n";

	return true;
}

bool generate_neovim_cpp(const QList<NeovimQt::Function> &ftable, QDir& dst)
{
	QFile f( dst.filePath(QLatin1String("neovim.cpp")) );
	if ( !f.open(QIODevice::WriteOnly) ) {
		printf("Unable to open neovim.cpp for writing\n");
		return false;
	}
	QTextStream out(&f);

	out << "// Auto generated " << QDateTime::currentDateTime().toString() << NL;
	out << "#include \"neovim.h\"\n";
	out << "#include \"neovimconnector.h\"\n";
	out << "namespace NeovimQt {\n";
	out << "Neovim::Neovim(NeovimConnector *c)\n";
	out << ":m_c(c)\n{\n}\n\n";

	foreach(NeovimQt::Function f, ftable) {
		out << QString("void Neovim::%1(").arg(f.name);
		for (int i=0; i<f.parameters.size(); i++) {
			if (i) {
				out << COMMA;
			}
			out << QString("%1 %2").arg(Type(f.parameters.at(i).first), f.parameters.at(i).second);
		}
		out << ")\n{\n";
		out << QString("\tNeovimRequest *r = m_c->startRequestUnchecked(\"%1\", %2);\n").arg(f.name).arg(f.parameters.size());
		out << QString("\tr->setFunction(Function::NEOVIM_FN_%1);\n").arg(f.name.toUpper());
		out << "\tconnect(r, &NeovimRequest::finished, this, &Neovim::handleResponse);\n";
		out << "\tconnect(r, &NeovimRequest::error, this, &Neovim::handleResponseError);\n";
		foreach(Param p, f.parameters) {
			out << QString("\tm_c->send(%1);\n").arg(p.second);
		}
		out << "}\n\n";
	}

	// dispatcher
	out << "void Neovim::handleResponseError(uint32_t msgid, Function::FunctionId fun, const QString& msg, const msgpack_object& res)\n{\n";
	out << "\temit error(msg);\n";
	out << "\tqDebug() << msg;\n";
	out << "\t}\n";

	out << "void Neovim::handleResponse(uint32_t msgid, Function::FunctionId fun, const msgpack_object& res)\n{\n";
	out << "\tbool convfail=true;\n";
	out << "\tswitch(fun) {\n";
	foreach(NeovimQt::Function f, ftable) {
		out << "\tcase Function::NEOVIM_FN_" << f.name.toUpper() << ":\n";
		out << "\t\t{\n"; // ctx
		if ( f.return_type != "void" ) {
			out << QString("\t\t\t%1 data = m_c->to_%1(res, &convfail);\n").arg(Type(f.return_type));
			out << "\t\t\tif (convfail) {\n";
			out << QString("\t\t\t\tqWarning() << \"Error unpacking data for signal %1\";\n").arg(f.name);
			out << "\t\t\t} else {\n";
			out << QString("\t\t\t\tqDebug() << \"%1 ->\" << data;\n").arg(f.name);
			out << QString("\t\t\t\temit on_%1(data);\n").arg(f.name);
			out << "\t\t\t}\n";
		} else {
    			out << "\t\t\tqDebug() << \"on_" << f.name << "\";\n";
			out << "\t\t\temit on_" << f.name << "();\n";
		}
		out << "\t\t}\n";
		out << "\t\tbreak;\n";
	}
	out << "\tdefault:\n";
	out << "\t\tqWarning() << \"Received unexpected response\";\n";
	out << "\t}\n";

	out << "}\n";
	out << "} // namespace\n";
	return true;
}

void usage()
{
	printf("Usage: nvim --api-info | neovimqtgen <output dir>");
}

int generate_bindings(const QString& path, const QList<NeovimQt::Function> &ftable)
{
	if (!QDir().mkpath(path)) {
		qFatal("Error creating output folder");
		return -1;
	}
	QDir dst(path);
	if ( !generate_function_enum(ftable, dst) ) {
		return -1;
	}
	if ( !generate_function_static(ftable, dst) ) {
		return -1;
	}
	if ( !generate_neovim_h(ftable, dst) ) {
		return -1;
	}
	if ( !generate_neovim_cpp(ftable, dst) ) {
		return -1;
	}

	printf("Generated Qt bindings for %d functions\n", ftable.size());
	return 0;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if ( app.arguments().size() > 2 ) {
		usage();
		return -1;
	}
	bool bindings = app.arguments().size() == 2;

	QFile in;;
	in.open(stdin, QIODevice::ReadOnly);
	if ( isatty(in.handle()) ) {
		usage();
		return -1;
	}

	QByteArray data = in.readAll();

	msgpack_unpacked msg;
	msgpack_unpacked_init(&msg);
	bool ok = msgpack_unpack_next(&msg, 
			data.constData(),
			data.size(), NULL);
	if ( !ok ) {
		printf("Unable to decode metadata");
		return -1;
	}

	if (msg.data.type != MSGPACK_OBJECT_MAP) {
		printf("Found unexpected data type for metadata description\n");
		return -1;
	}

	QList<NeovimQt::Function> ftable;
	for (uint32_t i=0; i< msg.data.via.map.size; i++) {
		const msgpack_object& key = msg.data.via.map.ptr[i].key;
		const msgpack_object& val = msg.data.via.map.ptr[i].val;
		if ( key.type != MSGPACK_OBJECT_RAW ) {
			printf("Found unexpected data type for metadata key(%d)\n", key.type);
			return -1;
		}
		QByteArray key_b = QByteArray(key.via.raw.ptr, key.via.raw.size);

		if ( key_b == "functions" ) {
			if ( val.type != MSGPACK_OBJECT_ARRAY ) {
				printf("Found unexpected data type for metadata function table");
				return -1;
			}

			for(uint32_t j=0; j<val.via.array.size; j++) {
				NeovimQt::Function f = NeovimQt::Function::fromMsgpack(val.via.array.ptr[j]);
				if ( !f.isValid() ) {
					printf("Unable to parse metadata function\n");
					return -1;
				}
				if (f.name == "vim_get_api_info") {
					// TODO: For now we skip this function, add later
					// after adding proper type decoding
					continue;
				}
				ftable.append(f);
			}
		}
	}

	if (bindings) {
		return generate_bindings(app.arguments().at(1), ftable);
	} else {
		printf("API info");
		foreach(const NeovimQt::Function f, ftable) {
			QTextStream(stdout) << "  " << f.signature() << "\n";
		}
	}

	return 0;
}

