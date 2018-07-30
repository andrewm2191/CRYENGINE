// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include <StdAfx.h>
#include "Grid.h"

#include "Objects/ISelectionGroup.h"
#include "Objects/BaseObject.h"

#include <QWidget>
#include <QVBoxLayout>

#include <CrySerialization/Math.h>
#include <CrySerialization/yasli/decorators/Range.h>

#include <IUndoObject.h>

class CSnappingPreferencesDialog : public CDockableWidget
{
public:
	CSnappingPreferencesDialog(QWidget* parent = nullptr)
	{
		QVBoxLayout* pLayout = new QVBoxLayout();
		pLayout->setMargin(0);
		pLayout->setSpacing(0);

		pLayout->addWidget(GetIEditor()->GetPreferences()->GetPageWidget("General/Snapping"));
		setLayout(pLayout);
	}

	virtual IViewPaneClass::EDockingDirection GetDockingDirection() const override { return IViewPaneClass::DOCK_FLOAT; }
	virtual const char*                       GetPaneTitle() const override        { return "Snapping Preferences"; }
	virtual QRect                             GetPaneRect() override               { return QRect(0, 0, 270, 200); }
};

REGISTER_HIDDEN_VIEWPANE_FACTORY(CSnappingPreferencesDialog, "Snap Settings", "Settings", true)

EDITOR_COMMON_API SSnappingPreferences gSnappingPreferences;
REGISTER_PREFERENCES_PAGE_PTR(SSnappingPreferences, &gSnappingPreferences)

bool SSnappingPreferences::Serialize(yasli::Archive& ar)
{
	ar.openBlock("gridSnapping", "Grid Snapping");
	ar(yasli::Range(m_gridSize, 0.001, 10000.0), "gridSize", "Grid Size");
	ar(yasli::Range(m_gridScale, 0.001, 10000.0), "gridScale", "Grid Scale");
	ar(yasli::Range(m_gridMajorLine, 1, 10000), "gridMajorLine", "Grid Major Line");
	ar.closeBlock();

	ar.openBlock("angleSnapping", "Angle Snapping");
	ar(yasli::Range(m_angleSnap, 0.001, 360.0), "angleSnap", "Angle Snap");
	ar.closeBlock();

	ar.openBlock("scaleSnapping", "Scale Snapping");
	ar(yasli::Range(m_scaleSnap, 0.01f, 1000.0f), "scaleSnap", "Scale Snap");
	ar.closeBlock();
	return true;
}

double SSnappingPreferences::SnapAngle(double angle) const
{
	if (!m_angleSnappingEnabled)
		return angle;
	return floor(angle / m_angleSnap + 0.5) * m_angleSnap;
}

Ang3 SSnappingPreferences::SnapAngle(const Ang3& vec) const
{
	if (!m_angleSnappingEnabled)
		return vec;
	Ang3 snapped;
	snapped.x = floor(vec.x / m_angleSnap + 0.5) * m_angleSnap;
	snapped.y = floor(vec.y / m_angleSnap + 0.5) * m_angleSnap;
	snapped.z = floor(vec.z / m_angleSnap + 0.5) * m_angleSnap;
	return snapped;
}

double SSnappingPreferences::SnapLength(const double length) const
{
	if (!m_gridSnappingEnabled || m_gridSize < 0.001)
		return length;
	return floor((length / m_gridSize) / m_gridScale + 0.5) * m_gridSize * m_gridScale;
}

float SSnappingPreferences::SnapScale(const float scale) const
{
	if (!m_scaleSnappingEnabled)
		return scale;
	return floor(scale / m_scaleSnap + 0.5) * m_scaleSnap;
}

Vec3 SSnappingPreferences::Snap(const Vec3& vec, bool bForceSnap /*= false*/) const
{
	if ((!bForceSnap && !m_gridSnappingEnabled) || m_gridSize < 0.001)
		return vec;
	Vec3 snapped;
	snapped.x = floor((vec.x / m_gridSize) / m_gridScale + 0.5) * m_gridSize * m_gridScale;
	snapped.y = floor((vec.y / m_gridSize) / m_gridScale + 0.5) * m_gridSize * m_gridScale;
	snapped.z = floor((vec.z / m_gridSize) / m_gridScale + 0.5) * m_gridSize * m_gridScale;
	return snapped;
}

Vec3 SSnappingPreferences::Snap(const Vec3& vec, double fZoom) const
{
	if (!gridSnappingEnabled() || m_gridSize < 0.001f)
		return vec;

	double zoomscale = m_gridScale * fZoom;

	Vec3 snapped;
	snapped.x = floor((vec.x / m_gridSize) / zoomscale + 0.5) * m_gridSize * zoomscale;
	snapped.y = floor((vec.y / m_gridSize) / zoomscale + 0.5) * m_gridSize * zoomscale;
	snapped.z = floor((vec.z / m_gridSize) / zoomscale + 0.5) * m_gridSize * zoomscale;

	return snapped;
}

uint16 SSnappingPreferences::GetSnapMode() const
{
	return m_snapModeFlags;
}

void SSnappingPreferences::EnableSnapToTerrain(bool bEnable)
{
	m_snapModeFlags &= ~eTerrain;
	if (bEnable) // Disable geometry before enabling terrain snapping
		m_snapModeFlags = (m_snapModeFlags & ~eGeometry) | bEnable * eTerrain;
}

bool SSnappingPreferences::IsSnapToTerrainEnabled() const
{
	return m_snapModeFlags & eTerrain;
}

void SSnappingPreferences::EnableSnapToGeometry(bool bEnable)
{
	m_snapModeFlags &= ~eGeometry;
	if (bEnable) // Disable terrain before enabling geometry snapping
		m_snapModeFlags = (m_snapModeFlags & ~eTerrain) | bEnable * eGeometry;
}

bool SSnappingPreferences::IsSnapToGeometryEnabled() const
{
	return m_snapModeFlags & eGeometry;
}

void SSnappingPreferences::EnableSnapToNormal(bool bEnable)
{
	m_snapModeFlags = (m_snapModeFlags & ~eSurfaceNormal) | bEnable * eSurfaceNormal;
}

bool SSnappingPreferences::IsSnapToNormalEnabled() const
{
	return m_snapModeFlags & eSurfaceNormal;
}

void SSnappingPreferences::EnablePivotSnapping(bool bEnable)
{
	m_bPivotSnappingEnabled = bEnable;
}

bool SSnappingPreferences::IsPivotSnappingEnabled() const
{
	return m_bPivotSnappingEnabled;
}

namespace Private_SnapCommands
{
void AlignToGrid()
{
	const ISelectionGroup* sel = GetIEditor()->GetISelectionGroup();
	if (sel->GetCount() == 0)
	{
		CUndo undo("Align To Grid");
		Matrix34 tm;
		for (int i = 0; i < sel->GetCount(); i++)
		{
			CBaseObject* obj = sel->GetObject(i);
			tm = obj->GetWorldTM();
			// Force snapping on
			Vec3 snappedPos = gSnappingPreferences.Snap(tm.GetTranslation(), true);
			tm.SetTranslation(snappedPos);
			obj->SetWorldTM(tm);
			obj->OnEvent(EVENT_ALIGN_TOGRID);
		}
	}
}
}

REGISTER_EDITOR_AND_SCRIPT_COMMAND(Private_SnapCommands::AlignToGrid, level, align_to_grid,
                                   CCommandDescription("Aligns selected objects to grid"));
REGISTER_EDITOR_UI_COMMAND_DESC(level, align_to_grid, "Align to Grid", "", "icons:Navigation/Align_To_Grid.ico", false)
REGISTER_COMMAND_REMAPPING(ui_action, actionAlign_To_Grid, level, align_to_grid)
