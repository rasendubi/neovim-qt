#include "mainwindow.h"

#include <QCloseEvent>
#include <QSettings>

namespace NeovimQt {

MainWindow::MainWindow(NeovimConnector *c, QWidget *parent)
:QMainWindow(parent), m_nvim(0), m_errorWidget(0), m_shell(0)
{
	m_errorWidget = new ErrorWidget();
	newDockWidgetFor(m_errorWidget);
	m_errorWidget->setVisible(false);

	init(c);
}

void MainWindow::init(NeovimConnector *c)
{
	if (m_shell) {
		m_shell->deleteLater();
	}
	if (m_nvim) {
		m_nvim->deleteLater();
	}

	m_nvim = c;
	m_shell = new Shell(c);
	setCentralWidget(m_shell);
	connect(m_shell, SIGNAL(neovimTitleChanged(const QString &)),
			this, SLOT(neovimSetTitle(const QString &)));
	connect(m_nvim, &NeovimConnector::processExited,
			this, &MainWindow::neovimExited);
	connect(m_nvim, &NeovimConnector::error,
			this, &MainWindow::neovimError);
	connect(m_errorWidget, &ErrorWidget::reconnectNeovim,
			this, &MainWindow::reconnectNeovim);
	m_shell->setFocus(Qt::OtherFocusReason);

	if (m_nvim->errorCause()) {
		neovimError(m_nvim->errorCause());
	}

	QSettings s;
	restoreGeometry(s.value("Window/Geometry").toByteArray());
	setMinimumWidth(320);
	setMinimumHeight(240);
}

QDockWidget* MainWindow::newDockWidgetFor(QWidget *w)
{
	QDockWidget *dock = new QDockWidget(this);
	dock->setAllowedAreas(Qt::TopDockWidgetArea);
	dock->setWidget(w);
	dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
	dock->setTitleBarWidget(new QWidget(this));
	addDockWidget(Qt::TopDockWidgetArea, dock);
	return dock;
}

/** The Neovim process has exited */
void MainWindow::neovimExited(int status)
{
	if (m_nvim->errorCause() != NeovimConnector::NoError) {
		m_errorWidget->setText(m_nvim->errorString());
		m_errorWidget->showReconnect(m_nvim->canReconnect());
		m_errorWidget->setVisible(true);
	} else if (status != 0) {
		m_errorWidget->setText(QString("Neovim exited with status code (%1)").arg(status));
		m_errorWidget->showReconnect(m_nvim->canReconnect());
		m_errorWidget->setVisible(true);
	} else {
		close();
	}
}
void MainWindow::neovimError(NeovimConnector::NeovimError err)
{
	m_errorWidget->setText(m_nvim->errorString());
	m_errorWidget->showReconnect(m_nvim->canReconnect());
	m_errorWidget->setVisible(true);
}

void MainWindow::neovimSetTitle(const QString &title)
{
	this->setWindowTitle(title);
}

void MainWindow::reconnectNeovim()
{
	if (m_nvim->canReconnect()) {
		init(m_nvim->reconnect());
	}
	m_errorWidget->setVisible(false);
}

void MainWindow::saveState() 
{
	QSettings s;
	s.setValue("Window/Geometry", saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
	// Never unless the Neovim shell closes too
	if (m_shell->close()) {
		saveState();
		QWidget::closeEvent(ev);
	} else {
		ev->ignore();
	}
}

} // Namespace

