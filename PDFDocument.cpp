#include "PDFDocument.h"
#include "TeXDocument.h"
#include "QTeXApp.h"
#include "QTeXUtils.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QPainter>
#include <QPaintEngine>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <QDesktopWidget>
#include <QSettings>
#include <QScrollBar>
#include <QRegion>
#include <QVector>
#include <QHash>
#include <QList>
#include <QStack>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>

#include <math.h>

#include "GlobalParams.h"

#include "poppler-link.h"

#define SYNCTEX_EXT		".synctex"

const qreal kMaxScaleFactor = 8.0;
const qreal kMinScaleFactor = 0.125;

const int magSizes[] = { 200, 300, 400 };

// tool codes
const int kNone = 0;
const int kMagnifier = 1;
const int kScroll = 2;
const int kSelectText = 3;
const int kSelectImage = 4;

#pragma mark === PDFMagnifier ===

const int kMagFactor = 2;

PDFMagnifier::PDFMagnifier(QWidget *parent, qreal inDpi)
	: QLabel(parent)
	, page(NULL)
	, scaleFactor(kMagFactor)
	, dpi(inDpi)
	, imageDpi(0)
	, imagePage(NULL)
{
}

void PDFMagnifier::setPage(Poppler::Page *p, qreal scale)
{
	page = p;
	scaleFactor = scale * kMagFactor;
	if (page == NULL) {
		imagePage = NULL;
		image = QImage();
	}
	else {
		QWidget *parentWidget = qobject_cast<QWidget*>(parent());
		qreal newDpi = dpi * scaleFactor;
		QPoint newLoc = parentWidget->rect().topLeft() * kMagFactor;
		QSize newSize = parentWidget->rect().size() * kMagFactor;
		if (page != imagePage || newDpi != imageDpi || newLoc != imageLoc || newSize != imageSize)
			image = page->renderToImage(newDpi, newDpi,
										newLoc.x(), newLoc.y(),
										newSize.width(), newSize.height());
		imagePage = page;
		imageDpi = newDpi;
		imageLoc = newLoc;
		imageSize = newSize;
	}
	update();
}

void PDFMagnifier::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    drawFrame(&painter);
	painter.drawImage(event->rect(), image,
		event->rect().translated(x() * kMagFactor + width() / 2,
								 y() * kMagFactor + height() / 2));
}

void PDFMagnifier::resizeEvent(QResizeEvent * /*event*/)
{
	QSettings settings;
	if (settings.value("circularMagnifier", kDefault_CircularMagnifier).toBool()) {
		int side = qMin(width(), height());
		QRegion maskedRegion(width() / 2 - side / 2, height() / 2 - side / 2, side, side, QRegion::Ellipse);
		setMask(maskedRegion);
	}
}

#pragma mark === PDFWidget ===

QCursor *PDFWidget::magnifierCursor = NULL;
QCursor *PDFWidget::zoomInCursor = NULL;
QCursor *PDFWidget::zoomOutCursor = NULL;

PDFWidget::PDFWidget()
	: QLabel()
	, document(NULL)
	, page(NULL)
	, clickedLink(NULL)
	, pageIndex(0)
	, scaleFactor(1.0)
	, dpi(72.0)
	, scaleOption(kFixedMag)
	, magnifier(NULL)
	, usingTool(kNone)
{
	QSettings settings;
	dpi = settings.value("previewResolution", QApplication::desktop()->logicalDpiX()).toInt();
	
	setBackgroundRole(QPalette::Base);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	setFocusPolicy(Qt::StrongFocus);
	setScaledContents(true);
	setMouseTracking(true);

	if (magnifierCursor == NULL) {
		magnifierCursor = new QCursor(QPixmap(":/images/images/magnifiercursor.png"));
		zoomInCursor = new QCursor(QPixmap(":/images/images/zoomincursor.png"));
		zoomOutCursor = new QCursor(QPixmap(":/images/images/zoomoutcursor.png"));
	}
}

void PDFWidget::setDocument(Poppler::Document *doc)
{
	document = doc;
	reloadPage();
}

void PDFWidget::windowResized()
{
	switch (scaleOption) {
		case kFixedMag:
			break;
		case kFitWidth:
			fitWidth(true);
			break;
		case kFitWindow:
			fitWindow(true);
			break;
	}
}

void PDFWidget::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	drawFrame(&painter);

	qreal newDpi = dpi * scaleFactor;
	QRect newRect = rect();
	if (page != imagePage || newDpi != imageDpi || newRect != imageRect)
		image = page->renderToImage(dpi * scaleFactor, dpi * scaleFactor,
									rect().x(), rect().y(),
									rect().width(), rect().height());
	imagePage = page;
	imageDpi = newDpi;
	imageRect = newRect;

	painter.drawImage(event->rect(), image, event->rect());

	if (!highlightBoxes.isEmpty()) {
		painter.setRenderHint(QPainter::Antialiasing);
		painter.scale(dpi / 72.27 * scaleFactor / 8, dpi / 72.27 * scaleFactor / 8);
		painter.setPen(QColor(0, 0, 0, 0));
		painter.setBrush(QColor(255, 255, 0, 63));
		foreach (const QRectF& box, highlightBoxes)
			painter.drawRect(box);
	}
}

void PDFWidget::useMagnifier(const QMouseEvent *inEvent)
{
	if (!magnifier) {
		magnifier = new PDFMagnifier(this, dpi);
		QSettings settings;
		int magnifierSize = settings.value("magnifierSize", kDefault_MagnifierSize).toInt();
		if (magnifierSize <= 0 || magnifierSize > (int)(sizeof(magSizes) / sizeof(int)))
			magnifierSize = kDefault_MagnifierSize;
		magnifierSize = magSizes[magnifierSize - 1];
		magnifier->setFixedSize(magnifierSize * 4 / 3, magnifierSize);
	}
	magnifier->setPage(page, scaleFactor);
	// this was in the hope that if the mouse is released before the image is ready,
	// the magnifier wouldn't actually get shown. but it doesn't seem to work that way -
	// the MouseMove event that we're posting must end up ahead of the mouseUp
	QMouseEvent *event = new QMouseEvent(QEvent::MouseMove, inEvent->pos(), inEvent->globalPos(), inEvent->button(), inEvent->buttons(), inEvent->modifiers());
	QCoreApplication::postEvent(this, event);
	usingTool = kMagnifier;
}

// Mouse control for the various tools:
// * magnifier
//   - ctrl-click to sync
//   - click to use magnifier
//   - shift-click to zoom in
//   - shift-click and drag to zoom to selected area
//   - alt-click to zoom out
// * scroll (hand)
//   - ctrl-click to sync
//   - click and drag to scroll
//   - double-click to use magnifier
// * select area (crosshair)
//   - ctrl-click to sync
//   - click and drag to select area
//   - double-click to use magnifier
// * select text (I-beam)
//   - ctrl-click to sync
//   - click and drag to select text
//   - double-click to use magnifier

static QPoint scrollClickPos;
static Qt::KeyboardModifiers mouseDownModifiers;

void PDFWidget::mousePressEvent(QMouseEvent *event)
{
	clickedLink = NULL;
	mouseDownModifiers = event->modifiers();
	if (mouseDownModifiers & Qt::ControlModifier) {
		// ctrl key - this is a sync click, don't handle the mouseDown here
	}
	else {
		// check for click in link
		foreach (Poppler::Link* link, page->links()) {
			// poppler's linkArea is relative to the page rect, it seems
			QPointF scaledPos(event->pos().x() / scaleFactor / dpi * 72.0 / page->pageSizeF().width(),
								event->pos().y() / scaleFactor / dpi * 72.0 / page->pageSizeF().height());
			if (link->linkArea().contains(scaledPos)) {
				clickedLink = link;
				break;
			}
		}
		if (clickedLink == NULL) {
			switch (currentTool) {
				case kMagnifier:
					if (mouseDownModifiers & (Qt::ShiftModifier | Qt::AltModifier))
						; // do nothing - zoom in or out (on mouseUp)
					else
						useMagnifier(event);
					break;
				
				case kScroll:
					setCursor(Qt::ClosedHandCursor);
					scrollClickPos = event->globalPos();
					usingTool = kScroll;
					break;
			}
		}
	}
	event->accept();
}

void PDFWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (clickedLink != NULL) {
		QPointF scaledPos(event->pos().x() / scaleFactor / dpi * 72.0 / page->pageSizeF().width(),
							event->pos().y() / scaleFactor / dpi * 72.0 / page->pageSizeF().height());
		if (clickedLink->linkArea().contains(scaledPos)) {
			doLink(clickedLink);
		}
	}
	else {
		switch (usingTool) {
			case kNone:
				// Ctrl-click to sync
				if (mouseDownModifiers & Qt::ControlModifier) {
					if (event->modifiers() & Qt::ControlModifier) {
						QPointF unscaledPos(event->pos().x() / scaleFactor * dpi / 72.0,
											event->pos().y() / scaleFactor * dpi / 72.0);
						emit syncClick(pageIndex, unscaledPos);
					}
					break;
				}
				// check whether to zoom
				if (currentTool == kMagnifier) {
					Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
					if (mods & Qt::AltModifier)
						zoomOut();
					else if (mods & Qt::ShiftModifier)
						zoomIn();
				}
				break;
			case kMagnifier:
				magnifier->close();
				break;
		}
	}
	clickedLink = NULL;
	usingTool = kNone;
	updateCursor();
	event->accept();
}

void PDFWidget::doLink(const Poppler::Link *link)
{
	switch (link->linkType()) {
		case Poppler::Link::None:
			break;
		case Poppler::Link::Goto:
			{
				const Poppler::LinkGoto *go = dynamic_cast<const Poppler::LinkGoto*>(link);
				Q_ASSERT(go != NULL);
				if (go->isExternal()) {
					QString file = go->fileName();
					break; // FIXME -- we don't handle this yet!
				}
				Poppler::LinkDestination dest = go->destination();
				if (dest.pageNumber() > 0) {
					goToPage(dest.pageNumber() - 1);
					if (dest.isChangeZoom()) {
						// FIXME
					}
					QWidget *widget = window();
					PDFDocument*	doc = qobject_cast<PDFDocument*>(widget);
					if (doc) {
						QScrollArea*	scrollArea = qobject_cast<QScrollArea*>(doc->centralWidget());
						if (scrollArea) {
							if (dest.isChangeLeft()) {
								int destLeft = (int)floor(dest.left() * scaleFactor * dpi / 72.0 * page->pageSizeF().width());
								scrollArea->horizontalScrollBar()->setValue(destLeft);
							}
							if (dest.isChangeTop()) {
								int destTop = (int)floor(dest.top() * scaleFactor * dpi / 72.0 * page->pageSizeF().height());
								scrollArea->verticalScrollBar()->setValue(destTop);
							}
						}
					}
				}
			}
			break;
		case Poppler::Link::Execute:
			break;
		case Poppler::Link::Browse:
			{
				const Poppler::LinkBrowse *browse = dynamic_cast<const Poppler::LinkBrowse*>(link);
				Q_ASSERT(browse != NULL);
				QString url = browse->url();
				if (!QDesktopServices::openUrl(QUrl(url)))
					QApplication::beep();
			}
			break;
		case Poppler::Link::Action:
			break;
		case Poppler::Link::Sound:
			break;
		case Poppler::Link::Movie:
			break;
	}
}

void PDFWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	useMagnifier(event);
	event->accept();
}

void PDFWidget::mouseMoveEvent(QMouseEvent *event)
{
	switch (usingTool) {
		case kMagnifier:
			magnifier->move(event->x() - magnifier->width() / 2, event->y() - magnifier->height() / 2);
			if (magnifier->isHidden()) {
				magnifier->show();
				setCursor(Qt::BlankCursor);
			}
			break;

		case kScroll:
			{
				QPoint delta = event->globalPos() - scrollClickPos;
				scrollClickPos = event->globalPos();
				QWidget *widget = window();
				PDFDocument*	doc = qobject_cast<PDFDocument*>(widget);
				if (doc) {
					QScrollArea*	scrollArea = qobject_cast<QScrollArea*>(doc->centralWidget());
					if (scrollArea) {
						int oldX = scrollArea->horizontalScrollBar()->value();
						scrollArea->horizontalScrollBar()->setValue(oldX - delta.x());
						int oldY = scrollArea->verticalScrollBar()->value();
						scrollArea->verticalScrollBar()->setValue(oldY - delta.y());
					}
				}
			}
			break;
		
		default:
			updateCursor(event->pos());
			break;
	}
	event->accept();
}

void PDFWidget::keyPressEvent(QKeyEvent *event)
{
	updateCursor();
	event->ignore();
}

void PDFWidget::keyReleaseEvent(QKeyEvent *event)
{
	updateCursor();
	event->ignore();
}

void PDFWidget::focusInEvent(QFocusEvent *event)
{
	updateCursor();
	event->ignore();
}

void PDFWidget::setTool(int tool)
{
	currentTool = tool;
	updateCursor();
}

void PDFWidget::updateCursor()
{
	if (usingTool != kNone)
		return;

	switch (currentTool) {
		case kScroll:
			setCursor(Qt::OpenHandCursor);
			break;
		case kMagnifier:
			{
				Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
				if (mods & Qt::AltModifier)
					setCursor(*zoomOutCursor);
				else if (mods & Qt::ShiftModifier)
					setCursor(*zoomInCursor);
				else
					setCursor(*magnifierCursor);
			}
			break;
		case kSelectText:
			setCursor(Qt::IBeamCursor);
			break;
		case kSelectImage:
			setCursor(Qt::CrossCursor);
			break;
	}
}

void PDFWidget::updateCursor(const QPoint& pos)
{
	// check for link
	foreach (Poppler::Link* link, page->links()) {
		// poppler's linkArea is relative to the page rect, it seems
		
		QPointF scaledPos(pos.x() / scaleFactor / dpi * 72.0 / page->pageSizeF().width(),
							pos.y() / scaleFactor / dpi * 72.0 / page->pageSizeF().height());
		if (link->linkArea().contains(scaledPos)) {
			setCursor(Qt::PointingHandCursor);
			return;
		}
	}
	updateCursor();
}

void PDFWidget::adjustSize()
{
	if (page) {
		QSize	pageSize = (page->pageSizeF() * scaleFactor * dpi / 72.0).toSize();
		if (pageSize != size())
			resize(pageSize);
	}
}

void PDFWidget::resetMagnifier()
{
	if (magnifier) {
		delete magnifier;
		magnifier = NULL;
	}
}

void PDFWidget::setResolution(int res)
{
	dpi = res;
	adjustSize();
	resetMagnifier();
}

void PDFWidget::setHighlightBoxes(const QList<QRectF>& boxlist)
{
	highlightBoxes = boxlist;
}

void PDFWidget::reloadPage()
{
	if (page != NULL)
		delete page;
	if (magnifier != NULL)
		magnifier->setPage(NULL, 0);
	imagePage = NULL;
	image = QImage();
	highlightBoxes.clear();
	page = document->page(pageIndex);
	adjustSize();
	update();
	updateStatusBar();
	emit changedPage(pageIndex);
}

void PDFWidget::updateStatusBar()
{
	QWidget *widget = window();
	PDFDocument *doc = qobject_cast<PDFDocument*>(widget);
	if (doc) {
		doc->showPage(pageIndex + 1);
		doc->showScale(scaleFactor);
	}
}

void PDFWidget::goFirst()
{
	if (pageIndex != 0) {
		pageIndex = 0;
		reloadPage();
		update();
	}
}

void PDFWidget::goPrev()
{
	if (pageIndex > 0) {
		--pageIndex;
		reloadPage();
		update();
	}
}

void PDFWidget::goNext()
{
	if (pageIndex < document->numPages() - 1) {
		++pageIndex;
		reloadPage();
		update();
	}
}

void PDFWidget::goLast()
{
	if (pageIndex != document->numPages() - 1) {
		pageIndex = document->numPages() - 1;
		reloadPage();
		update();
	}
}

void PDFWidget::doPageDialog()
{
	bool ok;
	setCursor(Qt::ArrowCursor);
	int pageNo = QInputDialog::getInteger(this, tr("Go to Page"),
									tr("Page number:"), pageIndex + 1,
									1, document->numPages(), 1, &ok);
	if (ok)
		goToPage(pageNo - 1);
}

void PDFWidget::goToPage(int p)
{
	if (p != pageIndex) {
		if (p >= 0 && p < document->numPages()) {
			pageIndex = p;
			reloadPage();
			update();
		}
	}
}

void PDFWidget::actualSize()
{
	scaleOption = kFixedMag;
	if (scaleFactor != 1.0) {
		scaleFactor = 1.0;
		adjustSize();
		update();
		updateStatusBar();
		emit changedZoom(scaleFactor);
	}
	emit changedScaleOption(scaleOption);
}

void PDFWidget::fitWidth(bool checked)
{
	if (checked) {
		scaleOption = kFitWidth;
		QWidget *widget = window();
		PDFDocument*	doc = qobject_cast<PDFDocument*>(widget);
		if (doc) {
			QScrollArea*	scrollArea = qobject_cast<QScrollArea*>(doc->centralWidget());
			if (scrollArea) {
				qreal portWidth = scrollArea->viewport()->width();
				QSizeF	pageSize = page->pageSizeF() * dpi / 72.0;
				scaleFactor = portWidth / pageSize.width();
				if (scaleFactor < kMinScaleFactor)
					scaleFactor = kMinScaleFactor;
				else if (scaleFactor > kMaxScaleFactor)
					scaleFactor = kMaxScaleFactor;
				adjustSize();
				update();
				updateStatusBar();
				emit changedZoom(scaleFactor);
			}
		}
	}
	else
		scaleOption = kFixedMag;
	emit changedScaleOption(scaleOption);
}

void PDFWidget::fitWindow(bool checked)
{
	if (checked) {
		scaleOption = kFitWindow;
		QWidget *widget = window();
		PDFDocument*	doc = qobject_cast<PDFDocument*>(widget);
		if (doc) {
			QScrollArea*	scrollArea = qobject_cast<QScrollArea*>(doc->centralWidget());
			if (scrollArea) {
				qreal portWidth = scrollArea->viewport()->width();
				qreal portHeight = scrollArea->viewport()->height();
				QSizeF	pageSize = page->pageSizeF() * dpi / 72.0;
				qreal sfh = portWidth / pageSize.width();
				qreal sfv = portHeight / pageSize.height();
				scaleFactor = sfh < sfv ? sfh : sfv;
				if (scaleFactor < kMinScaleFactor)
					scaleFactor = kMinScaleFactor;
				else if (scaleFactor > kMaxScaleFactor)
					scaleFactor = kMaxScaleFactor;
				adjustSize();
				update();
				updateStatusBar();
				emit changedZoom(scaleFactor);
			}
		}
	}
	else
		scaleOption = kFixedMag;
	emit changedScaleOption(scaleOption);
}

void PDFWidget::zoomIn()
{
	scaleOption = kFixedMag;
	if (scaleFactor < kMaxScaleFactor) {
		scaleFactor *= sqrt(2.0);
		if (fabs(scaleFactor - round(scaleFactor)) < 0.01)
			scaleFactor = round(scaleFactor);
		if (scaleFactor > kMaxScaleFactor)
			scaleFactor = kMaxScaleFactor;
		adjustSize();
		update();
		updateStatusBar();
		emit changedZoom(scaleFactor);
	}
	emit changedScaleOption(scaleOption);
}

void PDFWidget::zoomOut()
{
	scaleOption = kFixedMag;
	if (scaleFactor > kMinScaleFactor) {
		scaleFactor /= sqrt(2.0);
		if (fabs(scaleFactor - round(scaleFactor)) < 0.01)
			scaleFactor = round(scaleFactor);
		if (scaleFactor < kMinScaleFactor)
			scaleFactor = kMinScaleFactor;
		adjustSize();
		update();
		updateStatusBar();
		emit changedZoom(scaleFactor);
	}
	emit changedScaleOption(scaleOption);
}

void PDFWidget::saveState()
{
	saveScaleFactor = scaleFactor;
	saveScaleOption = scaleOption;
}

void PDFWidget::restoreState()
{
	if (scaleFactor != saveScaleFactor) {
		scaleFactor = saveScaleFactor;
		adjustSize();
		update();
		updateStatusBar();
		emit changedZoom(scaleFactor);
	}
	scaleOption = saveScaleOption;
	emit changedScaleOption(scaleOption);
}


#pragma mark === PDFDocument ===

QList<PDFDocument*> PDFDocument::docList;

PDFDocument::PDFDocument(const QString &fileName, TeXDocument *texDoc)
	: sourceDoc(texDoc)
{
	init();
	loadFile(fileName);
	if (texDoc != NULL)
		stackUnder((QWidget*)texDoc);
}

PDFDocument::~PDFDocument()
{
	docList.removeAll(this);
}

void
PDFDocument::init()
{
	docList.append(this);

	setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose, true);
	setAttribute(Qt::WA_MacNoClickThrough, true);
	setWindowIcon(QIcon(":/images/images/pdfdoc.png"));
		
	pdfWidget = new PDFWidget;
	connect(this, SIGNAL(windowResized()), pdfWidget, SLOT(windowResized()));

	toolButtonGroup = new QButtonGroup(toolBar);
	toolButtonGroup->addButton(qobject_cast<QAbstractButton*>(toolBar->widgetForAction(actionMagnify)), kMagnifier);
	toolButtonGroup->addButton(qobject_cast<QAbstractButton*>(toolBar->widgetForAction(actionScroll)), kScroll);
	toolButtonGroup->addButton(qobject_cast<QAbstractButton*>(toolBar->widgetForAction(actionSelect_Text)), kSelectText);
	toolButtonGroup->addButton(qobject_cast<QAbstractButton*>(toolBar->widgetForAction(actionSelect_Image)), kSelectImage);
	connect(toolButtonGroup, SIGNAL(buttonClicked(int)), pdfWidget, SLOT(setTool(int)));
	pdfWidget->setTool(kMagnifier);

	scaleLabel = new QLabel();
	statusBar()->addPermanentWidget(scaleLabel);
	scaleLabel->setFrameStyle(QFrame::StyledPanel);
	scaleLabel->setFont(statusBar()->font());

	pageLabel = new QLabel();
	statusBar()->addPermanentWidget(pageLabel);
	pageLabel->setFrameStyle(QFrame::StyledPanel);
	pageLabel->setFont(statusBar()->font());

	scrollArea = new QScrollArea;
	scrollArea->setBackgroundRole(QPalette::Dark);
	scrollArea->setAlignment(Qt::AlignCenter);
	scrollArea->setWidget(pdfWidget);
	setCentralWidget(scrollArea);

	document = NULL;
	
	connect(actionAbout_QTeX, SIGNAL(triggered()), qApp, SLOT(about()));

	connect(actionNew, SIGNAL(triggered()), qApp, SLOT(newFile()));
	connect(actionNew_from_Template, SIGNAL(triggered()), qApp, SLOT(newFromTemplate()));
	connect(actionOpen, SIGNAL(triggered()), qApp, SLOT(open()));

	connect(actionFirst_Page, SIGNAL(triggered()), pdfWidget, SLOT(goFirst()));
	connect(actionPrevious_Page, SIGNAL(triggered()), pdfWidget, SLOT(goPrev()));
	connect(actionNext_Page, SIGNAL(triggered()), pdfWidget, SLOT(goNext()));
	connect(actionLast_Page, SIGNAL(triggered()), pdfWidget, SLOT(goLast()));
	connect(actionGo_to_Page, SIGNAL(triggered()), pdfWidget, SLOT(doPageDialog()));
	connect(pdfWidget, SIGNAL(changedPage(int)), this, SLOT(enablePageActions(int)));

	connect(actionActual_Size, SIGNAL(triggered()), pdfWidget, SLOT(actualSize()));
	connect(actionFit_to_Width, SIGNAL(triggered(bool)), pdfWidget, SLOT(fitWidth(bool)));
	connect(actionFit_to_Window, SIGNAL(triggered(bool)), pdfWidget, SLOT(fitWindow(bool)));
	connect(actionZoom_In, SIGNAL(triggered()), pdfWidget, SLOT(zoomIn()));
	connect(actionZoom_Out, SIGNAL(triggered()), pdfWidget, SLOT(zoomOut()));
	connect(actionFull_Screen, SIGNAL(triggered()), this, SLOT(toggleFullScreen()));
	connect(pdfWidget, SIGNAL(changedZoom(qreal)), this, SLOT(enableZoomActions(qreal)));
	connect(pdfWidget, SIGNAL(changedScaleOption(autoScaleOption)), this, SLOT(adjustScaleActions(autoScaleOption)));
	connect(pdfWidget, SIGNAL(syncClick(int, const QPointF&)), this, SLOT(syncClick(int, const QPointF&)));
	
	connect(actionTypeset, SIGNAL(triggered()), this, SLOT(retypeset()));
	
	connect(actionStack, SIGNAL(triggered()), qApp, SLOT(stackWindows()));
	connect(actionTile, SIGNAL(triggered()), qApp, SLOT(tileWindows()));
	connect(actionTile_Front_Two, SIGNAL(triggered()), qApp, SLOT(tileTwoWindows()));
	connect(actionGo_to_Source, SIGNAL(triggered()), this, SLOT(goToSource()));

	menuRecent = new QMenu(tr("Open Recent"));
	updateRecentFileActions();
	menuFile->insertMenu(actionOpen_Recent, menuRecent);
	menuFile->removeAction(actionOpen_Recent);

	connect(qApp, SIGNAL(recentFileActionsChanged()), this, SLOT(updateRecentFileActions()));
	connect(qApp, SIGNAL(windowListChanged()), this, SLOT(updateWindowMenu()));

	connect(actionPreferences, SIGNAL(triggered()), qApp, SLOT(preferences()));

	connect(this, SIGNAL(destroyed()), qApp, SLOT(updateWindowMenus()));

	connect(qApp, SIGNAL(syncPdf(const QString&, int)), this, SLOT(syncFromSource(const QString&, int)));

	QSettings settings;
	QTeXUtils::applyToolbarOptions(this, settings.value("toolBarIconSize", 2).toInt(), settings.value("toolBarShowText", false).toBool());
}
 
void PDFDocument::updateRecentFileActions()
{
	QTeXUtils::updateRecentFileActions(this, recentFileActions, menuRecent);
}

void PDFDocument::updateWindowMenu()
{
	QTeXUtils::updateWindowMenu(this, menuWindow);
}

void PDFDocument::selectWindow()
{
	show();
	raise();
	activateWindow();
}

void PDFDocument::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	emit windowResized();
}

void PDFDocument::loadFile(const QString &fileName)
{
	setCurrentFile(fileName);
	reload();
}

void PDFDocument::reload()
{
	QApplication::setOverrideCursor(Qt::WaitCursor);

	if (document != NULL)
		delete document;

	document = Poppler::Document::load(curFile);
	if (document != NULL) {
		document->setRenderBackend(Poppler::Document::SplashBackend);
		document->setRenderHint(Poppler::Document::Antialiasing);
		document->setRenderHint(Poppler::Document::TextAntialiasing);
		globalParams->setScreenType(screenDispersed);

		pdfWidget->setDocument(document);
		
		QApplication::restoreOverrideCursor();

		pdfWidget->setFocus();

		// FIXME: see if this takes long enough that we should offload it to a separate thread
		loadSyncData();
	}
	else
		statusBar()->showMessage(tr("Failed to load file \"%1\"").arg(QTeXUtils::strippedName(curFile)));
}

#define MAX_SYNC_LINE_LENGTH	(PATH_MAX + 256)

void PDFDocument::loadSyncData()
{
	pageSyncInfo.clear();
	tagToFile.clear();
	syncMag = 1.0;

	QFileInfo fi(curFile);
	QString syncName = fi.canonicalPath() + "/" + fi.completeBaseName() + SYNCTEX_EXT;
	fi.setFile(syncName);
	
	if (fi.exists()) {
		QFile	syncFile(syncName);
		if (syncFile.open(QIODevice::ReadOnly)) {
			int sheet = 0;
			int origin = 0;
			bool pdfMode = false;
			QStack<HBox> openBoxes;
			char data[MAX_SYNC_LINE_LENGTH];
			qint64 len;
			len = syncFile.readLine(data, MAX_SYNC_LINE_LENGTH);
			data[len] = 0;
			if (strncmp(data, "SyncTeX", 7) != 0 && strncmp(data, "synchronize", 11) != 0) {
				statusBar()->showMessage(tr("Unrecognized SyncTeX header line \"%1\"").arg(data), kStatusMessageDuration);
				goto done;
			}
			len = syncFile.readLine(data, MAX_SYNC_LINE_LENGTH);
			data[len] = 0;
			if (strncmp(data, "version:", 8) != 0) {
				int vers = atoi(data + 8);
				if (vers != 1) {
					statusBar()->showMessage(tr("Unrecognized SyncTeX format \"%1\"").arg(data), kStatusMessageDuration);
					goto done;
				}
			}
			while ((len = syncFile.readLine(data, MAX_SYNC_LINE_LENGTH)) > 0) {
				data[len - 1] = 0; // wipe out the end-of-line
				if (len > 1 && data[1] == ':') {
					switch (data[0]) {
						case '>':
							// >:pdf
							if (strncmp(data + 2, "pdf", 3) == 0)
								pdfMode = true;
							break;
						case 'z':
							// z:578
							if (sscanf(data + 2, "%d", &origin) == 1)
								;
							break;
						case 'm':
							// m:1200
							{
								int mag;
								if (sscanf(data + 2, "%d", &mag) == 1)
									syncMag = mag / 1000.0;
							}
							break;
						case 'i':
							// i:18:42MRKUK.TEV
							{
								int tag;
								if (sscanf(data + 2, "%d", &tag) != 1)
									break;
								char *filename = index(data + 2, ':');
								if (filename == NULL)
									break;
								++filename;
								if (*filename == 0)
									break;
								QFileInfo info(QFileInfo(curFile).absoluteDir(), filename);
								tagToFile[tag] = info.canonicalFilePath();
							}
							break;
						case 's':
							// s:1
							if (sscanf(data + 2, "%d", &sheet) != 1)
								break;
							while (pageSyncInfo.count() < sheet)
								pageSyncInfo.append(PageSyncInfo());
							openBoxes.clear();
							break;
						case 'h':
							// h:18:39(-578,3840,3368,4074)0
							if (sheet > 0) {
								int tag, line, x, y, w, h, d;
								if (sscanf(data + 2, "%d:%d(%d,%d,%d,%d,%d)", &tag, &line, &x, &y, &w, &h, &d) != 7)
									break;
								HBox hb = { tag, line, origin + x, origin + y, w, h, INT_MAX, -1 };
								openBoxes.push(hb);
							}
							break;
						case 'k':
							// k:2:8(2707,1536,-57)
							if (sheet > 0) {
								if (!openBoxes.isEmpty()) {
									HBox& hb = openBoxes.top();
									int tag, line, x, y, k;
									if (sscanf(data + 2, "%d:%d(%d,%d,%d)", &tag, &line, &x, &y, &k) != 5)
										break;
									if (tag == hb.tag) {
										if (line < hb.first)
											hb.first = line;
										if (line > hb.last)
											hb.last = line;
									}
								}
							}
							break;
						case 'g':
						case '$':
							// g:18:39(-578,3840)
							if (sheet > 0) {
								if (!openBoxes.isEmpty()) {
									HBox& hb = openBoxes.top();
									int tag, line, x, y;
									if (sscanf(data + 2, "%d:%d(%d,%d)", &tag, &line, &x, &y) != 4)
										break;
									if (tag == hb.tag) {
										if (line < hb.first)
											hb.first = line;
										if (line > hb.last)
											hb.last = line;
									}
								}
							}
							break;
						default:
							break;
					}
				}
				else if (data[0] == 'e' && data[1] <= ' ') {
					if (sheet > 0) {
						PageSyncInfo& psi = pageSyncInfo[sheet - 1];
						if (!openBoxes.isEmpty())
							psi.append(openBoxes.pop());
					}
				}
			}
			statusBar()->showMessage(tr("Loaded SyncTeX data: \"%1\"").arg(syncName), kStatusMessageDuration);
		done:
			syncFile.close();
		}
	}
	else
		statusBar()->showMessage(tr("No SyncTeX data available"), kStatusMessageDuration);
}

void PDFDocument::syncClick(int page, const QPointF& pos)
{
	if (page < pageSyncInfo.count()) {
		qreal offset = (72 - 72 * syncMag);
		QPointF syncPos((pos.x() - offset) * 8 / syncMag, (pos.y() - offset) * 8 / syncMag);
		const PageSyncInfo& psi = pageSyncInfo[page];
		foreach (const HBox& hb, psi) {
			QRectF r(hb.x, hb.y, hb.w, -hb.h);
			if (r.contains(syncPos.x(), syncPos.y())) {
				TeXDocument::openDocument(tagToFile[hb.tag], (hb.first < INT_MAX) ? hb.first : hb.line);
				break;
			}
		}
	}
}

void PDFDocument::syncFromSource(const QString& sourceFile, int lineNo)
{
	int tag = -1;
	foreach (int i, tagToFile.keys()) {
		if (tagToFile[i] == sourceFile) {
			tag = i;
			break;
		}
	}
	if (tag != -1) {
		QList<QRectF> boxlist;
		qreal offset = (72 - 72 * syncMag) * 8;
		int pageIndex = -1;
		for (int p = 0; pageIndex == -1 && p < pageSyncInfo.size(); ++p) {
			const PageSyncInfo& psi = pageSyncInfo[p];
			foreach (const HBox& hb, psi) {
				if (hb.tag != tag)
					continue;
				if (hb.first <= lineNo && hb.last >= lineNo) {
					if (pageIndex == -1)
						pageIndex = p;
					if (pageIndex == p)
						boxlist.append(QRectF(hb.x * syncMag + offset, hb.y * syncMag + offset,
												hb.w * syncMag, -hb.h * syncMag));
				}
			}
		}
		if (pageIndex != -1) {
			pdfWidget->goToPage(pageIndex);
			pdfWidget->setHighlightBoxes(boxlist);
			pdfWidget->update();
			selectWindow();
		}
	}
}

void PDFDocument::setCurrentFile(const QString &fileName)
{
	curFile = QFileInfo(fileName).canonicalFilePath();
	setWindowTitle(tr("%1[*] - %2").arg(QTeXUtils::strippedName(curFile)).arg(tr(TEXWORKS_NAME)));
	QTeXApp::instance()->updateWindowMenus();
}
 
PDFDocument *PDFDocument::findDocument(const QString &fileName)
{
	QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

	foreach (QWidget *widget, qApp->topLevelWidgets()) {
		PDFDocument *theDoc = qobject_cast<PDFDocument*>(widget);
		if (theDoc && theDoc->curFile == canonicalFilePath)
			return theDoc;
	}
	return NULL;
}

void PDFDocument::zoomToRight(QWidget *otherWindow)
{
	QDesktopWidget *desktop = QApplication::desktop();
	QRect screenRect = desktop->availableGeometry(otherWindow == NULL ? this : otherWindow);
	screenRect.setTop(screenRect.top() + 22);
	screenRect.setLeft((screenRect.left() + screenRect.right()) / 2 + 1);
	screenRect.setBottom(screenRect.bottom() - 1);
	screenRect.setRight(screenRect.right() - 1);
	setGeometry(screenRect);
}

void PDFDocument::showPage(int page)
{
	pageLabel->setText(tr("page %1 of %2").arg(page).arg(document->numPages()));
}

void PDFDocument::showScale(qreal scale)
{
	scaleLabel->setText(tr("%1%").arg(round(scale * 10000.0) / 100.0));
}

void PDFDocument::retypeset()
{
	if (sourceDoc != NULL)
		sourceDoc->typeset();
}

void PDFDocument::interrupt()
{
	if (sourceDoc != NULL)
		sourceDoc->interrupt();
}

void PDFDocument::goToSource()
{
	if (sourceDoc != NULL)
		sourceDoc->selectWindow();
}

void PDFDocument::enablePageActions(int pageIndex)
{
// disabling these leads to a crash if we hit the end of document while auto-repeating a key
// (seems like a Qt bug, but needs further investigation)
//	actionFirst_Page->setEnabled(pageIndex > 0);
//	actionPrevious_Page->setEnabled(pageIndex > 0);
//	actionNext_Page->setEnabled(pageIndex < document->numPages() - 1);
//	actionLast_Page->setEnabled(pageIndex < document->numPages() - 1);
}

void PDFDocument::enableZoomActions(qreal scaleFactor)
{
	actionZoom_In->setEnabled(scaleFactor < kMaxScaleFactor);
	actionZoom_Out->setEnabled(scaleFactor > kMinScaleFactor);
}

void PDFDocument::adjustScaleActions(autoScaleOption scaleOption)
{
	actionFit_to_Window->setChecked(scaleOption == kFitWindow);
	actionFit_to_Width->setChecked(scaleOption == kFitWidth);
}

void PDFDocument::toggleFullScreen()
{
	if (windowState() & Qt::WindowFullScreen) {
		// exiting full-screen mode
		statusBar()->show();
		toolBar->show();
		showNormal();
		pdfWidget->restoreState();
		actionFull_Screen->setChecked(false);
	}
	else {
		// entering full-screen mode
		statusBar()->hide();
		toolBar->hide();
		showFullScreen();
		pdfWidget->saveState();
		pdfWidget->fitWindow(true);
		actionFull_Screen->setChecked(true);
	}
}

void PDFDocument::resetMagnifier()
{
	pdfWidget->resetMagnifier();
}

void PDFDocument::setResolution(int res)
{
	if (res > 0)
		pdfWidget->setResolution(res);
}

void PDFDocument::enableTypesetAction(bool enabled)
{
	actionTypeset->setEnabled(enabled);
}

void PDFDocument::updateTypesettingAction(bool processRunning)
{
	if (processRunning) {
		disconnect(actionTypeset, SIGNAL(triggered()), this, SLOT(retypeset()));
		actionTypeset->setIcon(QIcon(":/images/tango/process-stop.png"));
		connect(actionTypeset, SIGNAL(triggered()), this, SLOT(interrupt()));
		enableTypesetAction(true);
	}
	else {
		disconnect(actionTypeset, SIGNAL(triggered()), this, SLOT(interrupt()));
		actionTypeset->setIcon(QIcon(":/images/images/typeset.png"));
		connect(actionTypeset, SIGNAL(triggered()), this, SLOT(retypeset()));
	}
}
