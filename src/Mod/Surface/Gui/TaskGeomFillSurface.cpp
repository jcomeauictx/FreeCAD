/***************************************************************************
 *   Copyright (c) 2015 Balázs Bámer                                       *
 *                      Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <Gui/ViewProvider.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Command.h>
#include <Gui/SelectionObject.h>
#include <Base/Console.h>
#include <Gui/Control.h>
#include <Gui/BitmapFactory.h>
#include <Mod/Part/Gui/ViewProvider.h>

#include "TaskGeomFillSurface.h"
#include "ui_TaskGeomFillSurface.h"


using namespace SurfaceGui;

PROPERTY_SOURCE(SurfaceGui::ViewProviderGeomFillSurface, PartGui::ViewProviderSpline)

namespace SurfaceGui {

bool EdgeSelection::allow(App::Document* , App::DocumentObject* pObj, const char* sSubName)
{
    // don't allow references to itself
    if (pObj == editedObject)
        return false;
    if (!pObj->isDerivedFrom(Part::Feature::getClassTypeId()))
        return false;
    if (!sSubName || sSubName[0] == '\0')
        return false;
    std::string element(sSubName);
    if (element.substr(0,4) != "Edge")
        return false;
    auto links = editedObject->BoundaryList.getSubListValues();
    for (auto it : links) {
        if (it.first == pObj) {
            for (auto jt : it.second) {
                if (jt == sSubName)
                    return !appendEdges;
            }
        }
    }

    return appendEdges;
}

// ----------------------------------------------------------------------------

void ViewProviderGeomFillSurface::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    QAction* act;
    act = menu->addAction(QObject::tr("Edit filling"), receiver, member);
    act->setData(QVariant((int)ViewProvider::Default));
    PartGui::ViewProviderSpline::setupContextMenu(menu, receiver, member);
}

bool ViewProviderGeomFillSurface::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default ) {
        // When double-clicking on the item for this sketch the
        // object unsets and sets its edit mode without closing
        // the task panel

        Surface::GeomFillSurface* obj =  static_cast<Surface::GeomFillSurface*>(this->getObject());

        Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog();

        // start the edit dialog
        if (dlg) {
            TaskGeomFillSurface* tDlg = qobject_cast<TaskGeomFillSurface*>(dlg);
            if (tDlg)
                tDlg->setEditedObject(obj);
            Gui::Control().showDialog(dlg);
        }
        else {
            Gui::Control().showDialog(new TaskGeomFillSurface(this, obj));
        }
        return true;
    }
    else {
        return ViewProviderSpline::setEdit(ModNum);
    }
}

void ViewProviderGeomFillSurface::unsetEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        // when pressing ESC make sure to close the dialog
        QTimer::singleShot(0, &Gui::Control(), SLOT(closeDialog()));
    }
    else {
        PartGui::ViewProviderSpline::unsetEdit(ModNum);
    }
}

QIcon ViewProviderGeomFillSurface::getIcon(void) const
{
    return Gui::BitmapFactory().pixmap("BSplineSurf");
}

void ViewProviderGeomFillSurface::highlightReferences(bool on)
{
    Surface::GeomFillSurface* surface = static_cast<Surface::GeomFillSurface*>(getObject());
    auto bounds = surface->BoundaryList.getSubListValues();
    for (auto it : bounds) {
        Part::Feature* base = dynamic_cast<Part::Feature*>(it.first);
        if (base) {
            PartGui::ViewProviderPartExt* svp = dynamic_cast<PartGui::ViewProviderPartExt*>(
                        Gui::Application::Instance->getViewProvider(base));
            if (svp) {
                if (on) {
                    std::vector<App::Color> colors;
                    TopTools_IndexedMapOfShape eMap;
                    TopExp::MapShapes(base->Shape.getValue(), TopAbs_EDGE, eMap);
                    colors.resize(eMap.Extent(), svp->LineColor.getValue());

                    for (auto jt : it.second) {
                        std::size_t idx = static_cast<std::size_t>(std::stoi(jt.substr(4)) - 1);
                        assert (idx < colors.size());
                        colors[idx] = App::Color(1.0,0.0,1.0); // magenta
                    }

                    svp->setHighlightedEdges(colors);
                }
                else {
                    svp->unsetHighlightedEdges();
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------

GeomFillSurface::GeomFillSurface(ViewProviderGeomFillSurface* vp, Surface::GeomFillSurface* obj)
{
    ui = new Ui_GeomFillSurface();
    ui->setupUi(this);
    selectionMode = None;
    this->vp = vp;
    checkCommand = true;
    setEditedObject(obj);

    // Create context menu
    QAction* action = new QAction(tr("Remove"), this);
    action->setShortcut(QString::fromLatin1("Del"));
    ui->listWidget->addAction(action);
    connect(action, SIGNAL(triggered()), this, SLOT(onDeleteEdge()));
    ui->listWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
}

/*
 *  Destroys the object and frees any allocated resources
 */
GeomFillSurface::~GeomFillSurface()
{
    // no need to delete child widgets, Qt does it all for us
    delete ui;
}

// stores object pointer, its old fill type and adjusts radio buttons according to it.
void GeomFillSurface::setEditedObject(Surface::GeomFillSurface* obj)
{
    editedObject = obj;
    GeomFill_FillingStyle curtype = static_cast<GeomFill_FillingStyle>(editedObject->FillType.getValue());
    switch(curtype)
    {
    case GeomFill_StretchStyle:
        ui->fillType_stretch->setChecked(true);
        break;
    case GeomFill_CoonsStyle:
        ui->fillType_coons->setChecked(true);
        break;
    case GeomFill_CurvedStyle:
        ui->fillType_curved->setChecked(true);
        break;
    default:
        break;
    }

    auto objects = editedObject->BoundaryList.getValues();
    auto element = editedObject->BoundaryList.getSubValues();
    auto it = objects.begin();
    auto jt = element.begin();

    App::Document* doc = editedObject->getDocument();
    for (; it != objects.end() && jt != element.end(); ++it, ++jt) {
        QListWidgetItem* item = new QListWidgetItem(ui->listWidget);
        ui->listWidget->addItem(item);

        QString text = QString::fromLatin1("%1.%2")
                .arg(QString::fromUtf8((*it)->Label.getValue()))
                .arg(QString::fromStdString(*jt));
        item->setText(text);

        QList<QVariant> data;
        data << QByteArray(doc->getName());
        data << QByteArray((*it)->getNameInDocument());
        data << QByteArray(jt->c_str());
        item->setData(Qt::UserRole, data);
    }

    attachDocument(Gui::Application::Instance->getDocument(doc));
}

void GeomFillSurface::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

void GeomFillSurface::open()
{
    checkOpenCommand();
    this->vp->highlightReferences(true);
    Gui::Selection().clearSelection();
}

void GeomFillSurface::clearSelection()
{
    Gui::Selection().clearSelection();
}

void GeomFillSurface::checkOpenCommand()
{
    if (checkCommand && !Gui::Command::hasPendingCommand()) {
        std::string Msg("Edit ");
        Msg += editedObject->Label.getValue();
        Gui::Command::openCommand(Msg.c_str());
        checkCommand = false;
    }
}

void GeomFillSurface::slotUndoDocument(const Gui::Document&)
{
    checkCommand = true;
}

void GeomFillSurface::slotRedoDocument(const Gui::Document&)
{
    checkCommand = true;
}

bool GeomFillSurface::accept()
{
    this->vp->highlightReferences(false);
    selectionMode = None;
    Gui::Selection().rmvSelectionGate();

    int count = ui->listWidget->count();
    if (count > 4) {
        QMessageBox::warning(this,
            tr("Too many edges"),
            tr("The tool requires two, three or four edges"));
        return false;
    }
    else if (count < 2) {
        QMessageBox::warning(this,
            tr("Too less edges"),
            tr("The tool requires two, three or four edges"));
        return false;
    }

    if (editedObject->mustExecute())
        editedObject->recomputeFeature();
    if (!editedObject->isValid()) {
        QMessageBox::warning(this, tr("Invalid object"),
            QString::fromLatin1(editedObject->getStatusString()));
        return false;
    }

    Gui::Command::commitCommand();
    Gui::Command::doCommand(Gui::Command::Gui,"Gui.ActiveDocument.resetEdit()");
    Gui::Command::updateActive();
    return true;
}

bool GeomFillSurface::reject()
{
    this->vp->highlightReferences(false);
    selectionMode = None;
    Gui::Selection().rmvSelectionGate();

    Gui::Command::abortCommand();
    Gui::Command::doCommand(Gui::Command::Gui,"Gui.ActiveDocument.resetEdit()");
    Gui::Command::updateActive();
    return true;
}

void GeomFillSurface::on_fillType_stretch_clicked()
{
    changeFillType(GeomFill_StretchStyle);
}

void GeomFillSurface::on_fillType_coons_clicked()
{
    changeFillType(GeomFill_CoonsStyle);
}

void GeomFillSurface::on_fillType_curved_clicked()
{
    changeFillType(GeomFill_CurvedStyle);
}

void GeomFillSurface::changeFillType(GeomFill_FillingStyle fillType)
{
    GeomFill_FillingStyle curtype = static_cast<GeomFill_FillingStyle>(editedObject->FillType.getValue());
    if (curtype != fillType) {
        checkOpenCommand();
        editedObject->FillType.setValue(static_cast<long>(fillType));
        editedObject->recomputeFeature();
        if (!editedObject->isValid()) {
            Base::Console().Error("Surface filling: %s", editedObject->getStatusString());
        }
    }
}

void GeomFillSurface::on_buttonEdgeAdd_clicked()
{
    selectionMode = Append;
    Gui::Selection().addSelectionGate(new EdgeSelection(true, editedObject));
}

void GeomFillSurface::on_buttonEdgeRemove_clicked()
{
    selectionMode = Remove;
    Gui::Selection().addSelectionGate(new EdgeSelection(false, editedObject));
}

void GeomFillSurface::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (selectionMode == None)
        return;

    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        checkOpenCommand();
        if (selectionMode == Append) {
            QListWidgetItem* item = new QListWidgetItem(ui->listWidget);
            ui->listWidget->addItem(item);

            Gui::SelectionObject sel(msg);
            QString text = QString::fromLatin1("%1.%2")
                    .arg(QString::fromUtf8(sel.getObject()->Label.getValue()))
                    .arg(QString::fromLatin1(msg.pSubName));
            item->setText(text);

            QList<QVariant> data;
            data << QByteArray(msg.pDocName);
            data << QByteArray(msg.pObjectName);
            data << QByteArray(msg.pSubName);
            item->setData(Qt::UserRole, data);

            auto objects = editedObject->BoundaryList.getValues();
            objects.push_back(sel.getObject());
            auto element = editedObject->BoundaryList.getSubValues();
            element.push_back(msg.pSubName);
            editedObject->BoundaryList.setValues(objects, element);
            this->vp->highlightReferences(true);
        }
        else {
            Gui::SelectionObject sel(msg);
            QList<QVariant> data;
            data << QByteArray(msg.pDocName);
            data << QByteArray(msg.pObjectName);
            data << QByteArray(msg.pSubName);
            for (int i=0; i<ui->listWidget->count(); i++) {
                QListWidgetItem* item = ui->listWidget->item(i);
                if (item && item->data(Qt::UserRole) == data) {
                    ui->listWidget->takeItem(i);
                    delete item;
                }
            }

            this->vp->highlightReferences(false);
            App::DocumentObject* obj = sel.getObject();
            std::string sub = msg.pSubName;
            auto objects = editedObject->BoundaryList.getValues();
            auto element = editedObject->BoundaryList.getSubValues();
            auto it = objects.begin();
            auto jt = element.begin();
            for (; it != objects.end() && jt != element.end(); ++it, ++jt) {
                if (*it == obj && *jt == sub) {
                    objects.erase(it);
                    element.erase(jt);
                    editedObject->BoundaryList.setValues(objects, element);
                    break;
                }
            }
            this->vp->highlightReferences(true);
        }

        editedObject->recomputeFeature();
        QTimer::singleShot(50, this, SLOT(clearSelection()));
    }
}

void GeomFillSurface::onDeleteEdge()
{
    int row = ui->listWidget->currentRow();
    QListWidgetItem* item = ui->listWidget->item(row);
    if (item) {
        checkOpenCommand();
        QList<QVariant> data;
        data = item->data(Qt::UserRole).toList();
        ui->listWidget->takeItem(row);
        delete item;

        App::Document* doc = App::GetApplication().getDocument(data[0].toByteArray());
        App::DocumentObject* obj = doc ? doc->getObject(data[1].toByteArray()) : nullptr;
        std::string sub = data[2].toByteArray().constData();
        auto objects = editedObject->BoundaryList.getValues();
        auto element = editedObject->BoundaryList.getSubValues();
        auto it = objects.begin();
        auto jt = element.begin();
        this->vp->highlightReferences(false);
        for (; it != objects.end() && jt != element.end(); ++it, ++jt) {
            if (*it == obj && *jt == sub) {
                objects.erase(it);
                element.erase(jt);
                editedObject->BoundaryList.setValues(objects, element);
                break;
            }
        }
        this->vp->highlightReferences(true);
    }
}

// ----------------------------------------------------------------------------

TaskGeomFillSurface::TaskGeomFillSurface(ViewProviderGeomFillSurface* vp, Surface::GeomFillSurface* obj)
{
    widget = new GeomFillSurface(vp, obj);
    widget->setWindowTitle(QObject::tr("Surface"));
    taskbox = new Gui::TaskView::TaskBox(
        Gui::BitmapFactory().pixmap("BezSurf"),
        widget->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskGeomFillSurface::~TaskGeomFillSurface()
{
    // automatically deleted in the sub-class
}

void TaskGeomFillSurface::setEditedObject(Surface::GeomFillSurface* obj)
{
    widget->setEditedObject(obj);
}

void TaskGeomFillSurface::open()
{
    widget->open();
}

bool TaskGeomFillSurface::accept()
{
    return widget->accept();
}

bool TaskGeomFillSurface::reject()
{
    return widget->reject();
}

}

#include "moc_TaskGeomFillSurface.cpp"