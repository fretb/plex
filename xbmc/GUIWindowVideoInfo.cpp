/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "GUIWindow.h"
#include "GUIWindowVideoInfo.h"
#include "Util.h"
#include "Picture.h"
#include "GUIImage.h"
#include "StringUtils.h"
#include "GUIWindowVideoBase.h"
#include "GUIWindowVideoFiles.h"
#include "GUIDialogFileBrowser.h"
#include "utils/GUIInfoManager.h"
#include "VideoInfoScanner.h"
#include "Application.h"
#include "VideoInfoTag.h"
#include "GUIWindowManager.h"
#include "GUIDialogOK.h"
#include "GUIDialogYesNo.h"
#include "GUIDialogSelect.h"
#include "GUIDialogProgress.h"
#include "FileSystem/Directory.h"
#include "FileSystem/File.h"
#include "FileItem.h"
#include "MediaManager.h"
#include "utils/AsyncFileCopy.h"
#include "Settings.h"
#include "AdvancedSettings.h"
#include "GUISettings.h"
#include "LocalizeStrings.h"
#include "GUIUserMessages.h"
#include "TextureCache.h"
#include "StackDirectory.h"
#include "PlexDirectory.h"
#include "HTTP.h"
#include "PlexUtils.h"
#include "CocoaUtilsPlus.h"

using namespace std;
using namespace XFILE;

#define CONTROL_IMAGE                3
#define CONTROL_TEXTAREA             4
#define CONTROL_BTN_TRACKS           5
#define CONTROL_BTN_REFRESH          6
#define CONTROL_BTN_PLAY             8
#define CONTROL_BTN_RESUME           9
#define CONTROL_BTN_GET_THUMB       10
#define CONTROL_BTN_PLAY_TRAILER    11
#define CONTROL_BTN_GET_FANART      12

#define CONTROL_LIST                50

CGUIWindowVideoInfo::CGUIWindowVideoInfo(void)
    : CGUIDialog(WINDOW_VIDEO_INFO, "DialogVideoInfo.xml")
    , m_movieItem(new CFileItem)
{
  m_bRefreshAll = true;
  m_bRefresh = false;
  m_hasUpdatedThumb = false;
  m_castList = new CFileItemList;
}

CGUIWindowVideoInfo::~CGUIWindowVideoInfo(void)
{
  delete m_castList;
}

bool CGUIWindowVideoInfo::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_WINDOW_DEINIT:
    {
      ClearCastList();
    }
    break;

  case GUI_MSG_WINDOW_INIT:
    {
      m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);

      m_bRefresh = false;
      m_bRefreshAll = true;
      m_hasUpdatedThumb = false;

      CGUIDialog::OnMessage(message);
      m_bViewReview = true;
      Refresh();

      CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_REFRESH, (g_settings.GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Left(2).Equals("xx"));
      CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_GET_THUMB, (g_settings.GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Mid(2).Equals("plugin"));

      VIDEODB_CONTENT_TYPE type = GetContentType(m_movieItem.get());
      if (type == VIDEODB_CONTENT_TVSHOWS || type == VIDEODB_CONTENT_MOVIES)
        CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_GET_FANART, (g_settings.GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Mid(2).Equals("plugin"));
      else
        CONTROL_DISABLE(CONTROL_BTN_GET_FANART);

      return true;
    }
    break;


  case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      if (iControl == CONTROL_BTN_REFRESH)
      {
        if (m_movieItem->GetVideoInfoTag()->m_iSeason < 0 && !m_movieItem->GetVideoInfoTag()->m_strShowTitle.IsEmpty()) // tv show
        {
          bool bCanceled=false;
          if (CGUIDialogYesNo::ShowAndGetInput(20377,20378,-1,-1,bCanceled))
          {
            m_bRefreshAll = true;
            CVideoDatabase db;
            if (db.Open())
            {
              db.SetPathHash(m_movieItem->GetVideoInfoTag()->m_strPath,"");
              db.Close();
            }
          }
          else
            m_bRefreshAll = false;

          if (bCanceled)
            return false;
        }
        m_bRefresh = true;
        Close();
        return true;
      }
      else if (iControl == CONTROL_BTN_TRACKS)
      {
        m_bViewReview = !m_bViewReview;
        Update();
      }
      else if (iControl == CONTROL_BTN_PLAY)
      {
        Play();
      }
      else if (iControl == CONTROL_BTN_RESUME)
      {
        Play(true);
      }
      else if (iControl == CONTROL_BTN_GET_THUMB)
      {
        OnGetThumb();
      }
      else if (iControl == CONTROL_BTN_PLAY_TRAILER)
      {
        PlayTrailer();
      }
      else if (iControl == CONTROL_BTN_GET_FANART)
      {
        OnGetFanart();
      }
      else if (iControl == CONTROL_LIST)
      {
        int iAction = message.GetParam1();
        if (ACTION_SELECT_ITEM == iAction || ACTION_MOUSE_LEFT_CLICK == iAction)
        {
          CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iControl);
          OnMessage(msg);
          int iItem = msg.GetParam1();
          if (iItem < 0 || iItem >= m_castList->Size())
            break;
          CStdString strItem = m_castList->Get(iItem)->GetLabel();
          CStdString strFind;
          strFind.Format(" %s ",g_localizeStrings.Get(20347));
          int iPos = strItem.Find(strFind);
          if (iPos == -1)
            iPos = strItem.size();
          CStdString tmp = strItem.Left(iPos);
          OnSearch(tmp);
        }
      }
    }
    break;
  case GUI_MSG_NOTIFY_ALL:
    {
      if (IsActive() && message.GetParam1() == GUI_MSG_UPDATE_ITEM && message.GetItem())
      {
        CFileItemPtr item = boost::static_pointer_cast<CFileItem>(message.GetItem());
        if (item && m_movieItem->m_strPath.Equals(item->m_strPath))
        { // Just copy over the stream details and the thumb if we don't already have one
          if (!m_movieItem->HasThumbnail())
            m_movieItem->SetThumbnailImage(item->GetThumbnailImage());
          m_movieItem->GetVideoInfoTag()->m_streamDetails = item->GetVideoInfoTag()->m_streamDetails;
        }
        return true;
      }
    }
  }

  return CGUIDialog::OnMessage(message);
}

void CGUIWindowVideoInfo::SetMovie(const CFileItemPtr& item)
{
  m_movieItem = item;
  
  ClearCastList();
  
  // Set the cast list appropriately.
  m_castList->SetContent(m_movieItem->GetProperty("mediaType"));
  
  if (m_castList->GetContent() == "movies")
  {
    // Compute the URL for the movie information.
    CURL url(item->m_strPath);
    
    // Is it a multipart item?
    if (item->IsStack())
    {
      CStackDirectory dir;
      url = dir.GetFirstStackedFile(item->m_strPath);
    }
    
    url.SetFileName("library/metadata/" + item->GetProperty("ratingKey"));
    url.SetOptions("?skipRefresh=1");
    
    // Download the data.
    CFileCurl set;
    CStdString strData;
    set.Get(url.Get(), strData);
    
    // Parse document.
    TiXmlDocument xmlDoc;
    if (!xmlDoc.Parse(strData)) return;
    
    // The container node.
    TiXmlElement* root = xmlDoc.RootElement();
    if (!root) return;
    
    // The Video node.
    TiXmlElement* video = root->FirstChildElement();
    if (!video) return;
    
    // The child nodes.
    TiXmlElement* role = video->FirstChildElement("Role");
    if (!role) return;
    
    while (role)
    {
      const char* strActor = role->Attribute("tag");
      const char* strRole  = role->Attribute("role");
      
      CStdString character;
      if (strRole == 0 || strlen(strRole) == 0)
        character = strActor;
      else
        character.Format("%s %s %s", strActor, g_localizeStrings.Get(20347).c_str(), strRole);
      
      CFileItemPtr item(new CFileItem(strActor));
      item->SetIconImage("DefaultActor.png");
      item->SetLabel(character);
      m_castList->Add(item);
      
      role = role->NextSiblingElement();
    }
  }
  
#if 0
  // setup cast list + determine type.  We need to do this here as it makes
  // sure that content type (among other things) is set correctly for the
  // old fixed id labels that we have floating around (they may be using
  // content type to determine visibility, so we'll set the wrong label)
  ClearCastList();
  VIDEODB_CONTENT_TYPE type = GetContentType(m_movieItem.get());
  if (type == VIDEODB_CONTENT_MUSICVIDEOS)
  { // music video
    CStdStringArray artists;
    StringUtils::SplitString(m_movieItem->GetVideoInfoTag()->m_strArtist, g_advancedSettings.m_videoItemSeparator, artists);
    for (std::vector<CStdString>::const_iterator it = artists.begin(); it != artists.end(); ++it)
    {
      CFileItemPtr item(new CFileItem(*it));
      if (CFile::Exists(item->GetCachedArtistThumb()))
        item->SetThumbnailImage(item->GetCachedArtistThumb());
      item->SetIconImage("DefaultArtist.png");
      m_castList->Add(item);
    }
    m_castList->SetContent("musicvideos");
  }
  else
  { // movie/show/episode
    for (CVideoInfoTag::iCast it = m_movieItem->GetVideoInfoTag()->m_cast.begin(); it != m_movieItem->GetVideoInfoTag()->m_cast.end(); ++it)
    {
      CStdString character;
      if (it->strRole.IsEmpty())
        character = it->strName;
      else
        character.Format("%s %s %s", it->strName.c_str(), g_localizeStrings.Get(20347).c_str(), it->strRole.c_str());
      CFileItemPtr item(new CFileItem(it->strName));
      if (CFile::Exists(item->GetCachedActorThumb()))
        item->SetThumbnailImage(item->GetCachedActorThumb());
      item->SetIconImage("DefaultActor.png");
      item->SetLabel(character);
      m_castList->Add(item);
    }
    // set fanart property for tvshows and movies
    if (type == VIDEODB_CONTENT_TVSHOWS || type == VIDEODB_CONTENT_MOVIES)
    {
      if (m_movieItem->CacheLocalFanart())
        m_movieItem->SetProperty("fanart_image",m_movieItem->GetCachedFanart());
    }
    // determine type:
    if (type == VIDEODB_CONTENT_TVSHOWS)
    {
      m_castList->SetContent("tvshows");
      // special case stuff for shows (not currently retrieved from the library in filemode (ref: GetTvShowInfo vs GetTVShowsByWhere)
      m_movieItem->m_dateTime.SetFromDateString(m_movieItem->GetVideoInfoTag()->m_strPremiered);
      if(m_movieItem->GetVideoInfoTag()->m_iYear == 0 && m_movieItem->m_dateTime.IsValid())
        m_movieItem->GetVideoInfoTag()->m_iYear = m_movieItem->m_dateTime.GetYear();
      m_movieItem->SetProperty("totalepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode);
      m_movieItem->SetProperty("numepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode); // info view has no concept of current watched/unwatched filter as we could come here from files view, but set for consistency
      m_movieItem->SetProperty("watchedepisodes", m_movieItem->GetVideoInfoTag()->m_playCount);
      m_movieItem->SetProperty("unwatchedepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode - m_movieItem->GetVideoInfoTag()->m_playCount);
      m_movieItem->GetVideoInfoTag()->m_playCount = (m_movieItem->GetVideoInfoTag()->m_iEpisode == m_movieItem->GetVideoInfoTag()->m_playCount) ? 1 : 0;
    }
    else if (type == VIDEODB_CONTENT_EPISODES)
    {
      m_castList->SetContent("episodes");
      // special case stuff for episodes (not currently retrieved from the library in filemode (ref: GetEpisodeInfo vs GetEpisodesByWhere)
      m_movieItem->m_dateTime.SetFromDateString(m_movieItem->GetVideoInfoTag()->m_strFirstAired);
      if(m_movieItem->GetVideoInfoTag()->m_iYear == 0 && m_movieItem->m_dateTime.IsValid())
        m_movieItem->GetVideoInfoTag()->m_iYear = m_movieItem->m_dateTime.GetYear();
      if (CFile::Exists(m_movieItem->GetCachedEpisodeThumb()))
        m_movieItem->SetThumbnailImage(m_movieItem->GetCachedEpisodeThumb());
      // retrieve the season thumb.
      // NOTE: This is overly complicated. Perhaps we should cache season thumbs by showtitle and season number,
      //       rather than bothering with show path and the localized strings involved?
      if (m_movieItem->GetVideoInfoTag()->m_iSeason > -1)
      {
        CStdString label;
        if (m_movieItem->GetVideoInfoTag()->m_iSeason == 0)
          label = g_localizeStrings.Get(20381);
        else
          label.Format(g_localizeStrings.Get(20358), m_movieItem->GetVideoInfoTag()->m_iSeason);
        CFileItem season(label);
        season.m_bIsFolder = true;
        // grab show path
        CVideoDatabase db;
        if (db.Open())
        {
          CFileItemList items;
          CStdString where = db.PrepareSQL("where c%02d='%s'", VIDEODB_ID_TV_TITLE, m_movieItem->GetVideoInfoTag()->m_strShowTitle.c_str());
          if (db.GetTvShowsByWhere("", where, items) && items.Size())
            season.GetVideoInfoTag()->m_strPath = items[0]->GetVideoInfoTag()->m_strPath;
          db.Close();
        }
        season.SetCachedSeasonThumb();
        if (season.HasThumbnail())
          m_movieItem->SetProperty("seasonthumb", season.GetThumbnailImage());
      }
    }
    else if (type == VIDEODB_CONTENT_MOVIES)
    {
      m_castList->SetContent("movies");

      // local trailers should always override non-local, so check 
      // for a local one if the registered trailer is online
      if (m_movieItem->GetVideoInfoTag()->m_strTrailer.IsEmpty() ||
          CUtil::IsInternetStream(m_movieItem->GetVideoInfoTag()->m_strTrailer))
      {
        CStdString localTrailer = m_movieItem->FindTrailer();
        if (!localTrailer.IsEmpty())
        {
          CVideoDatabase database;
          if(database.Open())
          {
            database.SetDetail(m_movieItem->GetVideoInfoTag()->m_strTrailer,
                               m_movieItem->GetVideoInfoTag()->m_iDbId,
                               VIDEODB_ID_TRAILER, VIDEODB_CONTENT_MOVIES);
            database.Close();
            CUtil::DeleteVideoDatabaseDirectoryCache();
            m_movieItem->GetVideoInfoTag()->m_strTrailer = localTrailer;
          }
        }
      }
    }
  }
  m_loader.LoadItem(m_movieItem.get());
#endif
}

void CGUIWindowVideoInfo::Update()
{
  // setup plot text area
  CStdString strTmp = m_movieItem->GetVideoInfoTag()->m_strPlot;
  if (!(!m_movieItem->GetVideoInfoTag()->m_strShowTitle.IsEmpty() && m_movieItem->GetVideoInfoTag()->m_iSeason == 0)) // dont apply to tvshows
    if (m_movieItem->GetVideoInfoTag()->m_playCount == 0 && !g_guiSettings.GetBool("videolibrary.showunwatchedplots"))
      strTmp = g_localizeStrings.Get(20370);

  strTmp.Trim();
  SetLabel(CONTROL_TEXTAREA, strTmp);

  CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_castList);
  OnMessage(msg);

  if (m_bViewReview)
  {
    if (!m_movieItem->GetVideoInfoTag()->m_strArtist.IsEmpty())
    {
      SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 133);
    }
    else
    {
      SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 206);
    }

    SET_CONTROL_HIDDEN(CONTROL_LIST);
    SET_CONTROL_VISIBLE(CONTROL_TEXTAREA);
  }
  else
  {
    SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 207);

    SET_CONTROL_HIDDEN(CONTROL_TEXTAREA);
    SET_CONTROL_VISIBLE(CONTROL_LIST);
  }

  // Check for resumability
  CGUIWindowVideoFiles *window = (CGUIWindowVideoFiles *)g_windowManager.GetWindow(WINDOW_VIDEO_FILES);
  if (window && window->GetResumeItemOffset(m_movieItem.get()) > 0)
    CONTROL_ENABLE(CONTROL_BTN_RESUME);
  else
    CONTROL_DISABLE(CONTROL_BTN_RESUME);

  CONTROL_ENABLE(CONTROL_BTN_PLAY);

  // update the thumbnail
  const CGUIControl* pControl = GetControl(CONTROL_IMAGE);
  if (pControl)
  {
    CGUIImage* pImageControl = (CGUIImage*)pControl;
    pImageControl->FreeResources();
    pImageControl->SetFileName(m_movieItem->GetThumbnailImage());
  }
  // tell our GUI to completely reload all controls (as some of them
  // are likely to have had this image in use so will need refreshing)
  if (m_hasUpdatedThumb)
  {
    CGUIMessage reload(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REFRESH_THUMBS);
    g_windowManager.SendMessage(reload);
  }
}

void CGUIWindowVideoInfo::Refresh()
{
  try
  {
    OutputDebugString("Refresh\n");

    CStdString strImage = m_movieItem->GetVideoInfoTag()->m_strPictureURL.GetFirstThumb().m_url;

    bool hasUpdatedThumb = false;
    CStdString thumbImage = m_movieItem->GetThumbnailImage();
    if (thumbImage.IsEmpty())
      thumbImage = m_movieItem->GetCachedVideoThumb();

    if (!CFile::Exists(thumbImage) || m_movieItem->GetProperty("HasAutoThumb") == "1")
    { // don't have a thumb already, try and grab one
      m_movieItem->SetUserVideoThumb();
      if (m_movieItem->GetThumbnailImage() != thumbImage)
        thumbImage = m_movieItem->GetThumbnailImage();
      if (!CFile::Exists(thumbImage) && strImage.size() > 0)
        CScraperUrl::DownloadThumbnail(thumbImage,m_movieItem->GetVideoInfoTag()->m_strPictureURL.GetFirstThumb());

      if (CFile::Exists(thumbImage))
      {
        if (m_movieItem->HasProperty("set_folder_thumb"))
          VIDEO::CVideoInfoScanner::ApplyThumbToFolder(m_movieItem->GetProperty("set_folder_thumb"), thumbImage);
        hasUpdatedThumb = true;
      }
    }

    if (hasUpdatedThumb)
    {
      m_movieItem->SetThumbnailImage(thumbImage);
      CUtil::DeleteVideoDatabaseDirectoryCache();
      m_hasUpdatedThumb = true;
    }

    Update();
  }
  catch (...)
  {}
}
bool CGUIWindowVideoInfo::NeedRefresh() const
{
  return m_bRefresh;
}

bool CGUIWindowVideoInfo::RefreshAll() const
{
  return m_bRefreshAll;
}

/// \brief Search the current directory for a string got from the virtual keyboard
void CGUIWindowVideoInfo::OnSearch(CStdString& strSearch)
{
  if (m_dlgProgress)
  {
    m_dlgProgress->SetHeading(194);
    m_dlgProgress->SetLine(0, strSearch);
    m_dlgProgress->SetLine(1, "");
    m_dlgProgress->SetLine(2, "");
    m_dlgProgress->StartModal();
    m_dlgProgress->Progress();
  }
  CFileItemList items;
  DoSearch(strSearch, items);

  if (m_dlgProgress)
    m_dlgProgress->Close();

  if (items.Size())
  {
    CGUIDialogSelect* pDlgSelect = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    pDlgSelect->Reset();
    pDlgSelect->SetHeading(283);

    for (int i = 0; i < (int)items.Size(); i++)
    {
      CFileItemPtr pItem = items[i];
      pDlgSelect->Add(pItem->GetLabel());
    }

    pDlgSelect->DoModal();

    int iItem = pDlgSelect->GetSelectedLabel();
    if (iItem < 0)
      return;

    CFileItem* pSelItem = new CFileItem(*items[iItem]);

    OnSearchItemFound(pSelItem);

    delete pSelItem;
  }
  else
  {
    CGUIDialogOK::ShowAndGetInput(194, 284, 0, 0);
  }
}

/// \brief Make the actual search for the OnSearch function.
/// \param strSearch The search string
/// \param items Items Found
void CGUIWindowVideoInfo::DoSearch(CStdString& strSearch, CFileItemList& items)
{
  CVideoDatabase db;
  if (!db.Open())
    return;

  CFileItemList movies;
  db.GetMoviesByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20338) + "] ", items);

  db.GetTvShowsByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strShowTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20364) + "] ", items);

  db.GetEpisodesByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strTitle + " (" +  movies[i]->GetVideoInfoTag()->m_strShowTitle + ")";
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20359) + "] ", items);

  db.GetMusicVideosByArtist(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strArtist + " - " + movies[i]->GetVideoInfoTag()->m_strTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20391) + "] ", items);
  db.Close();
}

VIDEODB_CONTENT_TYPE CGUIWindowVideoInfo::GetContentType(const CFileItem *pItem) const
{
  VIDEODB_CONTENT_TYPE type = VIDEODB_CONTENT_MOVIES;
  if (pItem->HasVideoInfoTag() && !pItem->GetVideoInfoTag()->m_strShowTitle.IsEmpty()) // tvshow
    type = VIDEODB_CONTENT_TVSHOWS;
  if (pItem->HasVideoInfoTag() && pItem->GetVideoInfoTag()->m_iSeason > -1 && !pItem->m_bIsFolder) // episode
    type = VIDEODB_CONTENT_EPISODES;
  if (pItem->HasVideoInfoTag() && !pItem->GetVideoInfoTag()->m_strArtist.IsEmpty())
    type = VIDEODB_CONTENT_MUSICVIDEOS;
  return type;
}

/// \brief React on the selected search item
/// \param pItem Search result item
void CGUIWindowVideoInfo::OnSearchItemFound(const CFileItem* pItem)
{
#if 0
  VIDEODB_CONTENT_TYPE type = GetContentType(pItem);

  CVideoDatabase db;
  if (!db.Open())
    return;

  CVideoInfoTag movieDetails;
  if (type == VIDEODB_CONTENT_MOVIES)
    db.GetMovieInfo(pItem->m_strPath, movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_EPISODES)
    db.GetEpisodeInfo(pItem->m_strPath, movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_TVSHOWS)
    db.GetTvShowInfo(pItem->m_strPath, movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_MUSICVIDEOS)
    db.GetMusicVideoInfo(pItem->m_strPath, movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  db.Close();

  CFileItem item(*pItem);
  *item.GetVideoInfoTag() = movieDetails;
  SetMovie(&item);
  // refresh our window entirely
  Close();
  DoModal();
#endif
}

void CGUIWindowVideoInfo::ClearCastList()
{
  CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
  OnMessage(msg);
  m_castList->Clear();
}

void CGUIWindowVideoInfo::Play(bool resume)
{
  if (!m_movieItem->GetVideoInfoTag()->m_strEpisodeGuide.IsEmpty())
  {
    CStdString strPath;
    strPath.Format("videodb://2/2/%i/",m_movieItem->GetVideoInfoTag()->m_iDbId);
    Close();
    g_windowManager.ActivateWindow(WINDOW_VIDEO_NAV,strPath);
    return;
  }

  CFileItem movie(*m_movieItem->GetVideoInfoTag());
  if (m_movieItem->GetVideoInfoTag()->m_strFileNameAndPath.IsEmpty())
    movie.m_strPath = m_movieItem->m_strPath;
  CGUIWindowVideoFiles* pWindow = (CGUIWindowVideoFiles*)g_windowManager.GetWindow(WINDOW_VIDEO_FILES);
  if (pWindow)
  {
    // close our dialog
    Close(true);
    if (resume)
      movie.m_lStartOffset = STARTOFFSET_RESUME;
    else if (!CGUIWindowVideoBase::ShowResumeMenu(movie)) 
    {
      // The Resume dialog was closed without any choice
      DoModal();
      return;
    }
    pWindow->PlayMovie(&movie);
  }
}

// Get Thumb from user choice.
// Options are:
// 1.  Current thumb
// 2.  IMDb thumb
// 3.  Local thumb
// 4.  No thumb (if no Local thumb is available)
void CGUIWindowVideoInfo::OnGetThumb()
{
  string newPoster = OnGetMedia("posters", m_movieItem->GetThumbnailImage(), 20016);
  if (newPoster.size() > 0)
  {
    string newPosterFile = CFileItem::GetCachedPlexMediaServerThumb(newPoster);
    bool   success = true;
    
    if (CFile::Exists(newPosterFile) == false)
      success = AsyncDownloadMedia(newPoster, newPosterFile);
    
    if (success)
      m_movieItem->SetThumbnailImage(newPosterFile);
  }
#if 0
  CFileItemList items;

  // Current thumb
  if (CFile::Exists(m_movieItem->GetThumbnailImage()))
  {
    CFileItemPtr item(new CFileItem("thumb://Current", false));
    item->SetThumbnailImage(m_movieItem->GetThumbnailImage());
    item->SetLabel(g_localizeStrings.Get(20016));
    items.Add(item);
  }

  // Grab the thumbnails from the web
  vector<CStdString> thumbs;
  m_movieItem->GetVideoInfoTag()->m_strPictureURL.GetThumbURLs(thumbs);

  for (unsigned int i = 0; i < thumbs.size(); ++i)
  {
    CStdString strItemPath;
    strItemPath.Format("thumb://Remote%i", i);
    CFileItemPtr item(new CFileItem(strItemPath, false));
    item->SetThumbnailImage(thumbs[i]);
    item->SetIconImage("DefaultPicture.png");
    item->SetLabel(g_localizeStrings.Get(20015));

    // TODO: Do we need to clear the cached image?
    //    CTextureCache::Get().ClearCachedImage(thumb);
    items.Add(item);
  }

  CStdString localThumb(m_movieItem->GetUserVideoThumb());
  if (CFile::Exists(localThumb))
  {
    CFileItemPtr item(new CFileItem("thumb://Local", false));
    item->SetThumbnailImage(localThumb);
    item->SetLabel(g_localizeStrings.Get(20017));
    items.Add(item);
  }
  else
  { // no local thumb exists, so we are just using the IMDb thumb or cached thumb
    // which is probably the IMDb thumb.  These could be wrong, so allow the user
    // to delete the incorrect thumb
    CFileItemPtr item(new CFileItem("thumb://None", false));
    item->SetIconImage("DefaultVideo.png");
    item->SetLabel(g_localizeStrings.Get(20018));
    items.Add(item);
  }

  CStdString result;
  VECSOURCES sources(g_settings.m_videoSources);
  g_mediaManager.GetLocalDrives(sources);
  if (!CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(20019), result))
    return;   // user cancelled

  if (result == "thumb://Current")
    return;   // user chose the one they have

  // delete the thumbnail if that's what the user wants, else overwrite with the
  // new thumbnail
  CFileItem item(*m_movieItem->GetVideoInfoTag());
  CStdString cachedThumb(item.GetCachedVideoThumb());
  if (!m_movieItem->m_bIsFolder && m_movieItem->GetVideoInfoTag()->m_iSeason > -1)
    cachedThumb = item.GetCachedEpisodeThumb();
  CTextureCache::Get().ClearCachedImage(cachedThumb, true);

  if (result.Left(14) == "thumb://Remote")
  {
    int number = atoi(result.Mid(14));
    CFile::Cache(thumbs[number], cachedThumb);
  }
  else if (result == "thumb://Local")
    CFile::Cache(localThumb, cachedThumb);
  else if (CFile::Exists(result))
    CPicture::CreateThumbnail(result, cachedThumb);
  else
    result = "thumb://None";

  if (result == "thumb://None")
  {
    CFile::Delete(m_movieItem->GetCachedVideoThumb());
    CFile::Delete(m_movieItem->GetCachedEpisodeThumb());
    cachedThumb.Empty();
  }

  CUtil::DeleteVideoDatabaseDirectoryCache(); // to get them new thumbs to show
  m_movieItem->SetThumbnailImage(cachedThumb);
  if (m_movieItem->HasProperty("set_folder_thumb"))
  { // have a folder thumb to set as well
    VIDEO::CVideoInfoScanner::ApplyThumbToFolder(m_movieItem->GetProperty("set_folder_thumb"), cachedThumb);
  }
#endif
  m_hasUpdatedThumb = true;

  // Update our screen
  Update();

}

// Allow user to select a Fanart
void CGUIWindowVideoInfo::OnGetFanart()
{
  string newFanart = OnGetMedia("arts", m_movieItem->GetCachedFanart(), 20035);
  if (newFanart.size() > 0)
  {
    string newFanartFile = CFileItem::GetCachedPlexMediaServerFanart(newFanart);
    bool   success = true;
    
    if (CFile::Exists(newFanartFile) == false)
      success = AsyncDownloadMedia(newFanart, newFanartFile);
    
    if (success)
    {
      m_movieItem->SetQuickFanart(newFanart);
      m_movieItem->SetProperty("fanart_image", newFanartFile);
    }
  }
#if 0
  CFileItemList items;

  CFileItem item(*m_movieItem->GetVideoInfoTag());
  CStdString cachedThumb(item.GetCachedFanart());

  if (CFile::Exists(cachedThumb))
  {
    CFileItemPtr itemCurrent(new CFileItem("fanart://Current",false));
    itemCurrent->SetThumbnailImage(cachedThumb);
    itemCurrent->SetLabel(g_localizeStrings.Get(20440));
    items.Add(itemCurrent);
  }

  // ensure the fanart is unpacked
  m_movieItem->GetVideoInfoTag()->m_fanart.Unpack();

  // Grab the thumbnails from the web
  for (unsigned int i = 0; i < m_movieItem->GetVideoInfoTag()->m_fanart.GetNumFanarts(); i++)
  {
    CStdString strItemPath;
    strItemPath.Format("fanart://Remote%i",i);
    CFileItemPtr item(new CFileItem(strItemPath, false));
    CStdString thumb = m_movieItem->GetVideoInfoTag()->m_fanart.GetPreviewURL(i);
    item->SetThumbnailImage(CTextureCache::GetWrappedThumbURL(thumb));
    item->SetIconImage("DefaultPicture.png");
    item->SetLabel(g_localizeStrings.Get(20441));

    // TODO: Do we need to clear the cached image?
//    CTextureCache::Get().ClearCachedImage(thumb);
    items.Add(item);
  }

  CStdString strLocal = item.GetLocalFanart();
  if (!strLocal.IsEmpty())
  {
    CFileItemPtr itemLocal(new CFileItem("fanart://Local",false));
    itemLocal->SetThumbnailImage(strLocal);
    itemLocal->SetLabel(g_localizeStrings.Get(20438));

    // TODO: Do we need to clear the cached image?
    CTextureCache::Get().ClearCachedImage(strLocal);
    items.Add(itemLocal);
  }
  else
  {
    CFileItemPtr itemNone(new CFileItem("fanart://None", false));
    itemNone->SetIconImage("DefaultVideo.png");
    itemNone->SetLabel(g_localizeStrings.Get(20439));
    items.Add(itemNone);
  }

  CStdString result;
  VECSOURCES sources(g_settings.m_videoSources);
  g_mediaManager.GetLocalDrives(sources);
  bool flip=false;
  if (!CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(20437), result, &flip, 20445) || result.Equals("fanart://Current"))
    return;   // user cancelled

  CTextureCache::Get().ClearCachedImage(cachedThumb, true);

  if (result.Equals("fanart://Local"))
    result = strLocal;

  if (result.Left(15) == "fanart://Remote")
  {
    int iFanart = atoi(result.Mid(15).c_str());
    // set new primary fanart, and update our database accordingly
    m_movieItem->GetVideoInfoTag()->m_fanart.SetPrimaryFanart(iFanart);
    CVideoDatabase db;
    if (db.Open())
    {
      db.UpdateFanart(*m_movieItem, GetContentType(m_movieItem.get()));
      db.Close();
    }

    // download the fullres fanart image
    CStdString tempFile = "special://temp/fanart_download.jpg";
    CAsyncFileCopy downloader;
    bool succeeded = downloader.Copy(m_movieItem->GetVideoInfoTag()->m_fanart.GetImageURL(), tempFile, g_localizeStrings.Get(13413));
    if (succeeded)
    {
      if (flip)
        CPicture::ConvertFile(tempFile, cachedThumb,0,1920,-1,100,true);
      else
        CPicture::CacheFanart(tempFile, cachedThumb);
    }
    CFile::Delete(tempFile);
    if (!succeeded)
      return; // failed or cancelled download, so don't do anything
  }
  else if (CFile::Exists(result))
  { // local file
    if (flip)
      CPicture::ConvertFile(result, cachedThumb,0,1920,-1,100,true);
    else
      CPicture::CacheFanart(result, cachedThumb);
  }

  CUtil::DeleteVideoDatabaseDirectoryCache(); // to get them new thumbs to show
  if (CFile::Exists(cachedThumb))
    m_movieItem->SetProperty("fanart_image", cachedThumb);
  else
    m_movieItem->ClearProperty("fanart_image");
#endif
  m_hasUpdatedThumb = true;

  // Update our screen
  Update();
}

string CGUIWindowVideoInfo::OnGetMedia(const string& mediaType, const string& currentCachedMedia, int label)
{
  CFileItemList items;
  
  // Current one.
  if (currentCachedMedia.size() > 0 && CFile::Exists(currentCachedMedia))
  {
    CFileItemPtr itemCurrent(new CFileItem("media://Current", false));
    itemCurrent->SetThumbnailImage(currentCachedMedia);
    itemCurrent->SetLabel(g_localizeStrings.Get(label));
    items.Add(itemCurrent);
  }
  
  // Get a list of available ones.
  CFileItemList fileItems;
  CPlexDirectory plexDir;
  string url = m_movieItem->GetProperty("rootKey").c_str() + string("/") + mediaType;
  plexDir.GetDirectory(url, fileItems);
  
  m_mediaMap.clear();
  for (int i=0; i<fileItems.Size(); i++)
  {
    CFileItemPtr item = fileItems.Get(i);
    if (item->GetProperty("selected") == "0")
    {
      m_mediaMap[item->m_strPath] = item->GetProperty("ratingKey");
      item->SetLabel("");
      items.Add(item);
    }
  }
  
  // Have the user pick one.
  CStdString result;
  VECSOURCES sources(g_settings.m_videoSources);
  g_mediaManager.GetLocalDrives(sources);
  if (!CGUIDialogFileBrowser::ShowAndGetImageNoBrowse(items, sources, g_localizeStrings.Get(label), result))
    return "";
  
  if (result == "media://Current")
    return "";
  
  // Take the result and send it back, to the singular URL (e.g. PUT .../art)
  CStdString selectedKey = m_mediaMap[result];
  CUtil::URLEncode(selectedKey);
  
  CURL finalURL(CStdString(url.substr(0, url.size()-1)) + "?url=" + selectedKey);
  finalURL.SetProtocol("http");
  finalURL.SetPort(32400);
  
  CHTTP set;
  CStdString strData;
  set.Open(finalURL.Get(), "PUT", 0);
  set.ReadData(strData);
  set.Close();
  
  // Compute the new URL.
  finalURL.SetFileName(strData.substr(1));
  finalURL.SetOptions("");

  bool local = Cocoa_IsHostLocal(finalURL.GetHostName());
  return CPlexDirectory::BuildImageURL(url, finalURL.Get(), local);
}

bool CGUIWindowVideoInfo::AsyncDownloadMedia(const string& remoteFile, const string& localFile)
{
  CStdString tempFile = "special://temp/media_download.jpg";
  CAsyncFileCopy downloader;
  bool success = downloader.Copy(remoteFile, tempFile, g_localizeStrings.Get(13413));
  CPicture::CacheImage(tempFile, localFile);
  CFile::Delete(tempFile);
  
  return success;
}

void CGUIWindowVideoInfo::PlayTrailer()
{
  CFileItem item;
  item.m_strPath = m_movieItem->GetVideoInfoTag()->m_strTrailer;
  *item.GetVideoInfoTag() = *m_movieItem->GetVideoInfoTag();
  item.GetVideoInfoTag()->m_streamDetails.Reset();
  item.GetVideoInfoTag()->m_strTitle.Format("%s (%s)",m_movieItem->GetVideoInfoTag()->m_strTitle.c_str(),g_localizeStrings.Get(20410));
  item.SetThumbnailImage(m_movieItem->GetThumbnailImage());
  item.GetVideoInfoTag()->m_iDbId = -1;
  item.GetVideoInfoTag()->m_iFileId = -1;

  // Close the dialog.
  Close(true);

  if (item.IsPlayList())
    g_application.getApplicationMessenger().MediaPlay(item);
  else
    g_application.getApplicationMessenger().PlayFile(item);
}

void CGUIWindowVideoInfo::SetLabel(int iControl, const CStdString &strLabel)
{
  if (strLabel.IsEmpty())
  {
    SET_CONTROL_LABEL(iControl, 416);  // "Not available"
  }
  else
  {
    SET_CONTROL_LABEL(iControl, strLabel);
  }
}

const CStdString& CGUIWindowVideoInfo::GetThumbnail() const
{
  return m_movieItem->GetThumbnailImage();
}
