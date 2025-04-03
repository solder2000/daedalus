/*
Copyright (C) 2007 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/


#include "Base/Types.h"


#include <filesystem>
#include <sys/stat.h>
#include <chrono>
#include <iostream>
#include <string_view>

#include "Core/ROM.h"
#include "Interface/SaveState.h"
#include "Graphics/NativeTexture.h"
#include "DrawTextUtilities.h"
#include "UICommand.h"
#include "UIContext.h"
#include "UIElement.h"
#include "UIScreen.h"
#include "Menu.h"
#include "SavestateSelectorComponent.h"
#include "Utility/Paths.h"
#include "Utility/Stream.h"
#include "Utility/Translate.h"


class ISavestateSelectorComponent : public CSavestateSelectorComponent
{
	public:

		ISavestateSelectorComponent( CUIContext * p_context, EAccessType accetype, std::function<void (const char *)> on_slot_selected, const std::filesystem::path& running_rom );
		~ISavestateSelectorComponent();

		// CUIScreen
		virtual void				Update( float elapsed_time, const glm::vec2 & stick, u32 old_buttons, u32 new_buttons );
		virtual void				Render();
		virtual bool				IsFinished() const									{ return mIsFinished; }
	public:
		std::filesystem::path			current_slot_path;
		bool					isGameRunning;
	
	private:
		void				OnSlotSelected( u32 slot_idx );
		void				OnFolderSelected( u32 index );
		void				LoadFolders();
		void				LoadSlots();
		void				deleteSlot(u32 id_ss);
		bool				isDeletionAllowed;

	private:
		EAccessType				mAccessType;
		std::function<void(const char *)> 	mOnSlotSelected;

		u32		mSelectedSlot;
		bool	mIsFinished;
		bool	deleteButtonTriggered;
		bool	deleteConfirmTriggered;
		bool	isLoadingSlots;

		CUIElementBag				mElements;
		std::vector<std::string> 	mElementTitle;
		bool						mSlotEmpty[ NUM_SAVESTATE_SLOTS ];
		std::filesystem::path		mPVScreenShot [ NUM_SAVESTATE_SLOTS ];
		std::shared_ptr<CNativeTexture>	mPreviewTexture;
		u32							mLastPreviewLoad;

};


CSavestateSelectorComponent::~CSavestateSelectorComponent() {}


CSavestateSelectorComponent::CSavestateSelectorComponent( CUIContext * p_context )
:	CUIComponent( p_context )
{}


CSavestateSelectorComponent *	CSavestateSelectorComponent::Create( CUIContext * p_context, EAccessType accetype, std::function<void( const char *)>  on_slot_selected, const std::filesystem::path& running_rom )
{
	return new ISavestateSelectorComponent( p_context, accetype, on_slot_selected, running_rom );
}

namespace
{
	   void MakeSaveSlotPath(std::filesystem::path& path, std::filesystem::path& png_path, u32 slot_idx, const std::filesystem::path& slot_path)
    {
		//XXXX Todo Add support for alternative directories
        std::filesystem::path save_directory = setBasePath("SaveStates");
        std::filesystem::path full_directory = save_directory / slot_path;
		std::filesystem::create_directory(full_directory);

		std::string filename_png = FORMAT_NAMESPACE::format("saveslot{}.ss.png", slot_idx);
        std::string filename_ss = FORMAT_NAMESPACE::format("saveslot{}.ss", slot_idx);

        path = full_directory / filename_ss;
        png_path = full_directory / filename_png;
    }
}

ISavestateSelectorComponent::ISavestateSelectorComponent( CUIContext * p_context, EAccessType accetype, std::function<void (const char *)> on_slot_selected, const std::filesystem::path& running_rom )
:	CSavestateSelectorComponent( p_context )
,	mAccessType( accetype )
,	mOnSlotSelected( on_slot_selected )
,	mSelectedSlot( INVALID_SLOT )
,	mIsFinished( false )
,	mPreviewTexture ( nullptr )
,   isLoadingSlots ( false )
,	deleteButtonTriggered( false )
,	deleteConfirmTriggered( false )
{

	if(!running_rom.empty()){
		isGameRunning = true;
		isLoadingSlots = true;
		current_slot_path = running_rom;
		LoadSlots();
		isDeletionAllowed=true;
	} else {
		isGameRunning = false;
		LoadFolders();
		isDeletionAllowed=false;
	}
}

void ISavestateSelectorComponent::LoadFolders() {
    u32 folderIndex = 0;
    const char* const description_text = "";

    // Clear unused vector or perform any other necessary cleanup
    mElements.Clear();
    mLastPreviewLoad = ~0;

	isLoadingSlots = false;

    // Clear variables if needed
    for (u32 i = 0; i < NUM_SAVESTATE_SLOTS; ++i) {
		mPVScreenShot[i] = std::filesystem::path();
    }

	std::filesystem::path saveStateDir = setBasePath("SaveStates");
	for( const auto& entry : std::filesystem::directory_iterator(saveStateDir))
	{
		if (entry.is_directory()) 
		{
			std::string directoryName = entry.path().filename().string();
			if ((directoryName.size() > 2) && (directoryName != ".git"))
			{
				std::string str = directoryName;
				auto onSelected = [this, folderIndex]() { OnFolderSelected(folderIndex); };
				std::function<void()> functor = onSelected;
				auto element = std::make_unique<CUICommandImpl>(functor, str, description_text);
				mElements.Add(std::move(element));
				mElementTitle.push_back(directoryName);
				folderIndex++; 
			}
		}
	}
}
void ISavestateSelectorComponent::LoadSlots() {
    const char* description_text = "";
    char date_string[30];
	u32 elementIdx = 0;
    
    // Clear unused vector
    mElements.Clear();
	mPreviewTexture = nullptr;
    mLastPreviewLoad = ~0;
	isLoadingSlots = true;
	mSelectedSlot = INVALID_SLOT;

    for (u32 i = 0; i < NUM_SAVESTATE_SLOTS; ++i) {
    	std::string str = std::string("Slot ") + std::to_string(i + 1);
		 
        std::filesystem::path filename_ss;
        std::filesystem::path filename_png;
        MakeSaveSlotPath(filename_ss, filename_png, i, current_slot_path);

        if (std::filesystem::exists(filename_ss) == 1) {

            // stat the save file
            struct stat st;
            stat(filename_ss.c_str(), &st);

            // get the last modification time from the save file
            std::time_t modificationTime = st.st_mtime;

            // convert to localtime
            std::tm* timeinfo = std::localtime(&modificationTime);

            // fix the date
            timeinfo->tm_year -= 1900;
            timeinfo->tm_mon -= 1;

            // Format the date string
            std::strftime(date_string, sizeof(date_string), ": %m/%d/%Y %H:%M:%S", timeinfo);
            str += date_string;

            mSlotEmpty[i] = false;

			// Update screenshot array based on element position, not slot position
			mPVScreenShot[elementIdx] = filename_png;

        }
		// Don't show unused slots on loading
		else if (mAccessType == AT_LOADING )
		{
            mSlotEmpty[i] = true;
			continue;
		// Otherwise show empty slots
        } else {
            str = "Empty";
            mSlotEmpty[i] = true;
        }

        // Create UI elements
        std::unique_ptr<CUIElement> element = nullptr;
        auto onSelected = [this, i]() { OnSlotSelected(i); };
        std::function<void()> functor = onSelected;
        element = std::make_unique<CUICommandImpl>(functor, str.c_str(), description_text);
        mElements.Add(std::move(element));

		// increment element idx
		elementIdx++;
    }
}



ISavestateSelectorComponent::~ISavestateSelectorComponent() {}

void	ISavestateSelectorComponent::Update( float elapsed_time [[maybe_unused]], const glm::vec2 & stick [[maybe_unused]], [[maybe_unused]] u32 old_buttons, u32 new_buttons )
{
	//	Trigger the save on the first update AFTER mSelectedSlot was set.
	//	This ensures we get at least one frame where we can display "Saving..." etc.
	if( mSelectedSlot != INVALID_SLOT && !mIsFinished && !deleteButtonTriggered && !deleteConfirmTriggered)
	{
		mIsFinished = true;

		std::filesystem::path filename_ss;
		std::filesystem::path filename_png;
		MakeSaveSlotPath( filename_ss, filename_png, mSelectedSlot, current_slot_path );

		mPreviewTexture.reset();
		mOnSlotSelected( filename_ss.string().c_str());
	}

	// Delete confirmation
	if(mSelectedSlot != INVALID_SLOT && !mIsFinished && deleteConfirmTriggered)
	{
		// Delete the slot
		deleteSlot(mSelectedSlot);
		// Clear the flags
		deleteButtonTriggered=false;
		deleteConfirmTriggered=false;
		mSelectedSlot = INVALID_SLOT;
		// Reload the slots
		LoadSlots();
	}

	// Did player push a button?
	if(old_buttons != new_buttons)
	{
		// Player has selected to delete a slot
		if( deleteButtonTriggered )
		{
			if( new_buttons & PSP_CTRL_TRIANGLE && mSelectedSlot != INVALID_SLOT)
				deleteConfirmTriggered=true;

			// Player backs out or slot not selected
			if( new_buttons & (PSP_CTRL_CIRCLE|PSP_CTRL_SELECT)  || mSelectedSlot == INVALID_SLOT)
			{
				// Discard settings
				deleteButtonTriggered=false;
				deleteConfirmTriggered=false;
				mSelectedSlot = INVALID_SLOT;
			}
		}
		else
			{

			if( new_buttons & PSP_CTRL_UP )
			{
				mElements.SelectPrevious();
				if(mAccessType == AT_LOADING)
					deleteButtonTriggered=false;
			}
			if( new_buttons & PSP_CTRL_DOWN )
			{
				mElements.SelectNext();
				if(mAccessType == AT_LOADING)
					deleteButtonTriggered=false;
			}

			auto	element = mElements.GetSelectedElement();
			if( element != NULL )
			{
				if( new_buttons & PSP_CTRL_LEFT )
				{
					element->OnPrevious();
				}
				if( new_buttons & PSP_CTRL_RIGHT )
				{
					element->OnNext();
				}
				if( new_buttons & (PSP_CTRL_CROSS|PSP_CTRL_START) )
				{
					// Select Slot for Load
					element->OnSelected();
				}
				// delete savestate, use element selection so correct save state is removed
				if( new_buttons & PSP_CTRL_SQUARE && mAccessType == AT_LOADING && isDeletionAllowed )
				{
					deleteButtonTriggered=true;
					element->OnSelected();
				}
				// Player is backing out of menu
				if( new_buttons & (PSP_CTRL_CIRCLE|PSP_CTRL_SELECT) )
				{
					// Discard settings
					deleteButtonTriggered=false;
					deleteConfirmTriggered=false;
					mSelectedSlot = INVALID_SLOT;
					if(isGameRunning == false)
					{
						LoadFolders();
						isDeletionAllowed=false;
					}
					else
						mIsFinished = true;
				}
			}
		}
	}
}

void	ISavestateSelectorComponent::deleteSlot(u32 slot_idx)
{
	// Make the slot paths, check for files, then delete them
    std::filesystem::path filename_ss;
    std::filesystem::path filename_png;
    MakeSaveSlotPath( filename_ss, filename_png, slot_idx, current_slot_path );
				
    if (std::filesystem::exists(filename_ss))
        remove(filename_ss);

    if (std::filesystem::exists(filename_png))
        remove(filename_png);

}
void	ISavestateSelectorComponent::Render()
{
	const u32	font_height = mpContext->GetFontHeight();
	const s32	y = mpContext->GetScreenHeight() - (font_height / 2);

	s8 mPVExists = 0;
	std::filesystem::path filename_png;

	// No slot selected for load or slot selected for delete
	if( mSelectedSlot == INVALID_SLOT || deleteButtonTriggered )
	{
		mElements.Draw( mpContext, LIST_TEXT_LEFT, LIST_TEXT_WIDTH, AT_LEFT, BELOW_MENU_MIN + 30 - mElements.GetSelectedIndex()*(font_height+2) );

		// only load screen shots for slots, not directories
		if (isLoadingSlots)
		{

			filename_png = mPVScreenShot[ mElements.GetSelectedIndex() ];
			mPVExists = std::filesystem::is_regular_file(filename_png) ? 1 : -1;

			if( mPVExists == 1 )
			{
				// Render Preview Image
				glm::vec2	tl( PREVIEW_IMAGE_LEFT+2, BELOW_MENU_MIN+2 );
				glm::vec2	wh( PREVIEW_IMAGE_WIDTH-4, PREVIEW_IMAGE_HEIGHT-4 );

				if( mPreviewTexture == nullptr || mElements.GetSelectedIndex() != mLastPreviewLoad )
				{
					mPreviewTexture = CNativeTexture::CreateFromPng( filename_png.c_str() , TexFmt_8888 );
					mLastPreviewLoad = mElements.GetSelectedIndex();	
				}

				mpContext->DrawRect( PREVIEW_IMAGE_LEFT, BELOW_MENU_MIN, PREVIEW_IMAGE_WIDTH, PREVIEW_IMAGE_HEIGHT, c32::Black );
				mpContext->RenderTexture( mPreviewTexture, tl, wh, c32::White );
				
			}
			else if( mPVExists == -1 && mElements.GetSelectedIndex() < NUM_SAVESTATE_SLOTS )
			{
				mpContext->DrawRect( PREVIEW_IMAGE_LEFT, BELOW_MENU_MIN, PREVIEW_IMAGE_WIDTH, PREVIEW_IMAGE_HEIGHT, c32::Black );
				mpContext->DrawRect( PREVIEW_IMAGE_LEFT+2, BELOW_MENU_MIN+2, PREVIEW_IMAGE_WIDTH-4, PREVIEW_IMAGE_HEIGHT-4, c32::Black );
				mpContext->DrawTextAlign( PREVIEW_IMAGE_LEFT, PREVIEW_IMAGE_LEFT + PREVIEW_IMAGE_WIDTH, AT_CENTRE, BELOW_MENU_MIN+PREVIEW_IMAGE_HEIGHT/2, "No Preview Available", c32::White );
			}
		}

		// Render Available Action Text
    	if (deleteButtonTriggered)
			mpContext->DrawTextAlign( 0, mpContext->GetScreenWidth(), AT_CENTRE, y, "Delete selected savestate [/\\:confirm O:back]", DrawTextUtilities::TextRed );
		else if ( !isLoadingSlots )
			mpContext->DrawTextAlign( 0, mpContext->GetScreenWidth(), AT_CENTRE, y, "Select a game from which to load [X:select]", DrawTextUtilities::TextWhite );
		else if (mAccessType == AT_SAVING) 
			mpContext->DrawTextAlign( 0, mpContext->GetScreenWidth(), AT_CENTRE, y, "Select the slot in which to save [X:save O:back]", DrawTextUtilities::TextWhite );
		else if (mAccessType == AT_LOADING) 
			mpContext->DrawTextAlign( 0, mpContext->GetScreenWidth(), AT_CENTRE, y, "Select the slot from which to load [X:load O:back []:delete]", DrawTextUtilities::TextWhite );

	}
	// Slot selected, display Loading or Saving screen
	else
	{
		const char * title_text = mAccessType == AT_SAVING ? SAVING_STATUS_TEXT : LOADING_STATUS_TEXT;
		mpContext->DrawTextAlign( 0, mpContext->GetScreenWidth(), AT_CENTRE, y, title_text, mpContext->GetDefaultTextColour() );
	}

}


void	ISavestateSelectorComponent::OnSlotSelected( u32 slot_idx )
{
	if( slot_idx >= NUM_SAVESTATE_SLOTS )
		return;

	// Don't allow empty slots to be loaded
	if( mAccessType == AT_LOADING && mSlotEmpty[ slot_idx ] )
		return;

	mSelectedSlot = slot_idx;
}

void	ISavestateSelectorComponent::OnFolderSelected( u32 index )
{
	current_slot_path = mElementTitle[index].c_str();
	mElementTitle.clear();
	LoadSlots();
	isDeletionAllowed=true;
}
