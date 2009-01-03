/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"

// Panel has to be here to pass configure, so just test these
#if (defined(HAVE_LIBNCURSES) || defined (HAVE_LIBCURSES))

#include "kis_panel_widgets.h"
#include "kis_panel_frontend.h"
#include "timetracker.h"
#include "messagebus.h"

#define WIN_CENTER(h, w)   (LINES / 2) - ((h) / 2), (COLS / 2) - ((w) / 2), (h), (w)

unsigned int Kis_Panel_Specialtext::Strlen(string str) {
	int npos = 0;
	int escape = 0;

	for (unsigned int pos = 0; pos < str.size(); pos++) {
		if (str[pos] == '\004') {
			escape = 1;
			continue;
		}

		if (escape) {
			escape = 0;
			continue;
		}

		npos++;
	}

	return npos;
}

void Kis_Panel_Specialtext::Mvwaddnstr(WINDOW *win, int y, int x, string str, int n) {
	int npos = 0;
	int escape = 0;

	for (unsigned int pos = 0; pos < str.size(); pos++) {
		if (str[pos] == '\004') {
			escape = 1;
			continue;
		}

		// Handle the attributes
		if (escape) {
			if (str[pos] == 'u') {
				wattron(win, WA_UNDERLINE);
			} else if (str[pos] == 'U') {
				wattroff(win, WA_UNDERLINE);
			} else if (str[pos] == 's') {
				wattron(win, WA_STANDOUT);
			} else if (str[pos] == 'S') {
				wattroff(win, WA_STANDOUT);
			} else if (str[pos] == 'r') {
				wattron(win, WA_REVERSE);
			} else if (str[pos] == 'R') {
				wattroff(win, WA_REVERSE);
			} else if (str[pos] == 'b') {
				wattron(win, WA_BOLD);
			} else if (str[pos] == 'B') {
				wattroff(win, WA_BOLD);
			} else {
				// fprintf(stderr, "invalid escape '%c'\n", str[pos]);
				// Backfill the unescaped data
				escape = 0;
				if (npos <= n) {
					mvwaddch(win, y, x + npos, '\\');
					npos++;
				}
				if (npos <= n) {
					mvwaddch(win, y, x + npos, str[npos]);
					npos++;
				}
			}

			escape = 0;
			continue;
		}

		// Otherwise write the character, if we can.  We DON'T abort here,
		// because we need to process to the end of the string to turn off
		// any attributes that were on
		if (npos <= n) {
			mvwaddch(win, y, x + npos, str[pos]);
			npos++;
			continue;
		}
	}
}

Kis_Panel_Color::Kis_Panel_Color() {
	nextindex = COLORS + 1;
}

int Kis_Panel_Color::AddColor(string color) {
	map<string, int>::iterator cimi;
	int nums[2] = {0, 0};
	int bold;
	int pair;

	if ((cimi = color_index_map.find(StrLower(color))) != color_index_map.end()) {
		return cimi->second;
	}

	if (nextindex == COLOR_PAIRS - 1) {
		return 0;
	}

	vector<string> colorpair = StrTokenize(color, ",");

	if (colorpair.size() < 1)
		colorpair.push_back("white");
	if (colorpair.size() < 2)
		colorpair.push_back("black");

	colorpair[0] = StrLower(colorpair[0]);
	colorpair[1] = StrLower(colorpair[1]);

	for (unsigned int x = 0; x < 2; x++) {
		string clr = colorpair[x];
		
		// First, find if theres a hi-
		if (clr.substr(0, 3) == "hi-") {
			bold = 1;
			clr = clr.substr(3, clr.length() - 3);
		}

		// Then match all the colors
		if (clr == "black")
			nums[x] = COLOR_BLACK;
		else if (clr == "red")
			nums[x] = COLOR_RED;
		else if (clr == "green")
			nums[x] = COLOR_GREEN;
		else if (clr == "yellow")
			nums[x] = COLOR_YELLOW;
		else if (clr == "blue")
			nums[x] = COLOR_BLUE;
		else if (clr == "magenta")
			nums[x] = COLOR_MAGENTA;
		else if (clr == "cyan")
			nums[x] = COLOR_CYAN;
		else if (clr == "white")
			nums[x] = COLOR_WHITE;
	}

	init_pair(nextindex, nums[0], nums[1]);

	pair = COLOR_PAIR(nextindex);

	if (bold)
		pair |= A_BOLD;

	color_index_map[StrLower(color)] = pair;
	nextindex++;

	return pair;
}

int panelint_draw_timer(TIMEEVENT_PARMS) {
	return ((PanelInterface *) parm)->DrawInterface();
}

// Pollable panel interface driver
PanelInterface::PanelInterface() {
	fprintf(stderr, "FATAL OOPS:  PanelInterface() w/ no globalreg\n");
	exit(1);
}

PanelInterface::PanelInterface(GlobalRegistry *in_globalreg) {
	globalreg = in_globalreg;

	// Init curses
	initscr();
	raw();
	cbreak();
	noecho();
	keypad(stdscr, 1);
	meta(stdscr, 1);
	mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
	start_color();

	draweventid = 
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC / 2,
											  NULL, 1, &panelint_draw_timer,
											  (void *) this);

	globalreg->RegisterPollableSubsys(this);

	hsize = COLS;
	vsize = LINES;
};

PanelInterface::~PanelInterface() {
	for (unsigned int x = 0; x < live_panels.size(); x++)
		delete live_panels[x];

	globalreg->timetracker->RemoveTimer(draweventid);
	
	globalreg->RemovePollableSubsys(this);

	endwin();
}

unsigned int PanelInterface::MergeSet(unsigned int in_max_fd, fd_set *out_rset, 
									  fd_set *out_wset) {
	if (live_panels.size() == 0)
		return in_max_fd;

	// add stdin to the listen set
	FD_SET(fileno(stdin), out_rset);

	if ((int) in_max_fd < fileno(stdin))
		return fileno(stdin);

	return in_max_fd;
}

int PanelInterface::Poll(fd_set& in_rset, fd_set& in_wset) {
	if (live_panels.size() == 0)
		return 0;

	if (FD_ISSET(fileno(stdin), &in_rset)) {
		// Poll via the top of the stack
		int ret;
		
		ret = live_panels[live_panels.size() - 1]->Poll();
		DrawInterface();

		if (ret < 0)
			globalreg->fatal_condition = 1;
		return ret;
	}

	return 0;
}

void PanelInterface::ResizeInterface() {
	for (unsigned int x = 0; x < live_panels.size(); x++) {
		// If it's full screen, keep it full screen, otherwise
		// re-center it
		if (live_panels[x]->FetchSzy() == vsize &&
			live_panels[x]->FetchSzx() == hsize) {
			live_panels[x]->Position(0, 0, LINES, COLS);
		} else {
			live_panels[x]->Position(WIN_CENTER(live_panels[x]->FetchSzy(),
												live_panels[x]->FetchSzx()));
		}
	}
}

int PanelInterface::DrawInterface() {
	if (hsize != COLS || vsize != LINES) {
		ResizeInterface();
		hsize = COLS;
		vsize = LINES;
	}

	// Draw all the panels
	for (unsigned int x = 0; x < live_panels.size(); x++) {
		live_panels[x]->DrawPanel();
	}

	// Call the update
	update_panels();
	doupdate();

	// Delete dead panels from before
	for (unsigned int x = 0; x < dead_panels.size(); x++) {
		delete(dead_panels[x]);
	}
	dead_panels.clear();

	return 1;
}

void PanelInterface::AddPanel(Kis_Panel *in_panel) {
	live_panels.push_back(in_panel);
}

void PanelInterface::KillPanel(Kis_Panel *in_panel) {
	for (unsigned int x = 0; x < live_panels.size(); x++) {
		if (live_panels[x] == in_panel) {
			dead_panels.push_back(in_panel);
			live_panels.erase(live_panels.begin() + x);
		}
	}
}

Kis_Panel_Component::Kis_Panel_Component(GlobalRegistry *in_globalreg, 
										 Kis_Panel *in_panel) {
	globalreg = in_globalreg;
	parent_panel = in_panel;
	window = in_panel->FetchDrawWindow();
	visible = 0;
	active = 0;

	sx = sy = ex = ey = lx = ly = 0;
	px = py = 0;
	mx = my = 0;
	layout_dirty = 0;

	cb_switch = cb_activate = NULL;

	color_active = color_inactive = 0;

	name = "GENERIC_WIDGET";
}

void Kis_Panel_Component::SetCallback(int cbtype, int (*cb)(COMPONENT_CALLBACK_PARMS),
									  void *aux) {
	switch (cbtype) {
		case COMPONENT_CBTYPE_SWITCH:
			cb_switch = cb;
			cb_switch_aux = aux;
			break;
		case COMPONENT_CBTYPE_ACTIVATED:
			cb_activate = cb;
			cb_activate_aux = aux;
			break;
	}
}

void Kis_Panel_Component::ClearCallback(int cbtype) {
	switch (cbtype) {
		case COMPONENT_CBTYPE_SWITCH:
			cb_switch = NULL;
			break;
		case COMPONENT_CBTYPE_ACTIVATED:
			cb_activate = NULL;
			break;
	}
}

Kis_Panel_Packbox::Kis_Panel_Packbox(GlobalRegistry *in_globalreg,
									 Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	homogenous = 0;
	packing = 0;
	spacing = 0;
	center = 0;

	name = "GENERIC_PACKBOX";
}

Kis_Panel_Packbox::~Kis_Panel_Packbox() {
	// Nothing to do really
}

void Kis_Panel_Packbox::Pack_Start(Kis_Panel_Component *in_widget, int in_fill,
								   int in_padding) {
	packbox_details det;

	det.widget = in_widget;
	det.fill = in_fill;
	det.padding = in_padding;

	packed_items.push_front(det);

	layout_dirty = 1;
}

void Kis_Panel_Packbox::Pack_End(Kis_Panel_Component *in_widget, int in_fill,
								 int in_padding) {
	packbox_details det;

	det.widget = in_widget;
	det.fill = in_fill;
	det.padding = in_padding;

	packed_items.push_back(det);

	layout_dirty = 1;
}

void Kis_Panel_Packbox::Pack_Before_Named(string in_name, 
										  Kis_Panel_Component *in_widget, 
										  int in_fill, int in_padding) {
	list<Kis_Panel_Packbox::packbox_details>::iterator i;
	packbox_details det;

	det.widget = in_widget;
	det.fill = in_fill;
	det.padding = in_padding;

	layout_dirty = 1;

	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		if ((*i).widget->GetName() == in_name) {
			packed_items.insert(i, det);
			return;
		}
	}

	packed_items.push_back(det);
	return;
}

void Kis_Panel_Packbox::Pack_After_Named(string in_name, 
										 Kis_Panel_Component *in_widget, 
										 int in_fill, int in_padding) {
	list<Kis_Panel_Packbox::packbox_details>::iterator i;
	packbox_details det;

	det.widget = in_widget;
	det.fill = in_fill;
	det.padding = in_padding;

	layout_dirty = 1;

	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		if ((*i).widget->GetName() == in_name) {
			packed_items.insert(++i, det);
			return;
		}
	}

	packed_items.push_back(det);
	return;
}

void Kis_Panel_Packbox::Pack_Remove(Kis_Panel_Component *in_widget) {
	list<Kis_Panel_Packbox::packbox_details>::iterator i;

	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		if ((*i).widget == in_widget) {
			packed_items.erase(i);
			layout_dirty = 1;
			return;
		}
	}
}

void Kis_Panel_Packbox::Pack_Widgets() {
	int size, psize, msize, pos;
	list<Kis_Panel_Packbox::packbox_details>::iterator i;

	if (visible == 0)
		return;

	// Get the packing direction
	if (packing == 0) {
		size = lx;
	} else {
		size = ly;
	}

	// If we're homogenous, we divide by the # of widgets, find out if we're too
	// small for any of them, and decrease until we can fit them
	if (homogenous) {
		int ndivs = packed_items.size();
		int perbox = 0;

		for (i = packed_items.begin(); i != packed_items.end(); ++i) {
			int wmsize;

			if ((*i).widget->GetVisible() == 0) {
				ndivs--;
				continue;
			}

			perbox = (int) ((float) (size - (spacing * (ndivs - 1)))  / ndivs);

			if (packing == 0) {
				wmsize = (*i).widget->GetMinX();
			} else {
				wmsize = (*i).widget->GetMinY();
			}

			// If someone can't fit, decrease the number of divisions until we
			// can, and we just don't draw those widgets.  Yeah, it sucks, 
			// don't over-pack a small frame
			if (wmsize > perbox) {
				// If we simply can't fix the widget in, period, then bail on
				// drawing.
				if (ndivs <= 1) 
					return;

				ndivs -= 1;
				i = packed_items.begin();
				continue;
			}
		}

		i = packed_items.begin();
		for (int x = 0; x < ndivs && i != packed_items.end(); x++, ++i) {
			if ((*i).widget->GetVisible() == 0)
				continue;

			// Set the position of each widget
			int ww = perbox - ((*i).padding * 2);
			int co = 0;

			// Get the preferred size (or best we can do) OR the fill
			int psize = 0, op = 0;
			if ((*i).fill == 0) {
				if (packing == 0) {
					psize = (*i).widget->GetPrefX() + ((*i).padding * 2);
					/*
					op = (*i).widget->GetPrefY();
					if (op > ly || op == 0)
						op = ly;
					*/
					op = ly;
				} else {
					psize = (*i).widget->GetPrefY() + ((*i).padding * 2);
					/*
					op = (*i).widget->GetPrefX();
					if (op > lx || op == 0)
						op = lx;
						*/
					op = lx;
				}

				if (psize > ww)
					psize = ww;
			} else {
				psize = ww;
			}

			if (center && psize != ww) {
				co = (ww - psize) / 2;
			}

			if (packing == 0) {
				(*i).widget->SetPosition(
						sx + (perbox * x) + (*i).padding + co, sy,
						sx + (perbox * x) + (*i).padding + co + psize, sy + op);
			} else {
				(*i).widget->SetPosition(
						sx, sy + (perbox * x) + (*i).padding + co, sx + op, 
						sy + (perbox * x) + (*i).padding + co + psize);
			}

		}

		return;
		// Done w/ homogenous spacing
	} 

	// Non-homogenous spacing
	// Pass 1: Can we fit everyone who has a preferred size in?  If we can, then
	// we can just start expanding them (or just plain draw them as is if we 
	// don't have any filler).  Calculate preferred and minimum sizes simultaneously
	// to save another iteration.
	psize = 0;
	msize = 0;
	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		if ((*i).widget->GetVisible() == 0)
			continue;

		if (packing == 0) {
			psize += (*i).widget->GetPrefX() + ((*i).padding * 2);
			msize += (*i).widget->GetMinX() + ((*i).padding * 2);
		} else {
			psize += (*i).widget->GetPrefY() + ((*i).padding * 2);
			msize += (*i).widget->GetMinY() + ((*i).padding * 2);
		}
	}

	// If we can't fit the preferred, can we fit the minimum?
	if (psize > size) {
		// fprintf(stderr, "debug - %p can't fit preferred\n", this);
		if (msize <= size) {
			// fprintf(stderr, "debug - %p can fit in size\n", this);
			pos = 0;
			// Fit them via minsize, giving them space from the free
			// bucket so long as we have it
			int bucket = size - msize;

			// fprintf(stderr, "debug - %p has bucket %d for items, min %d\n", this, bucket, msize);

			i = packed_items.begin();
			for (int x = 0; i != packed_items.end(); ++i, x++) {
				if ((*i).widget->GetVisible() == 0)
					continue;

				int mp, pp, op;

				if (packing == 0) {
					mp = (*i).widget->GetMinX();
					pp = (*i).widget->GetPrefX();
					/*
					op = (*i).widget->GetPrefY();
					if (op > ly || op == 0)
						op = ly;
						*/
					op = ly;
				} else {
					mp = (*i).widget->GetMinY();
					pp = (*i).widget->GetPrefY();
					/*
					op = (*i).widget->GetPrefX();
					if (op > lx || op == 0)
						op = lx;
						*/
					op = lx;
				}

				int ww;
				ww = mp + ((*i).padding * 2);
				// fprintf(stderr, "debug - %p item %d gets %d\n", this, x, ww);
				if (bucket > 0 && mp < pp) {
					int delta = pp - mp;
					if (delta > bucket)
						delta = bucket;
					// fprintf(stderr, "debug - %p gave %d to item %d min %d wanted %d was %d now %d\n", this, delta, x, mp, pp - mp, ww, ww+delta);
					bucket -= delta;
					ww += delta;
				}

				if (packing == 0) {
					(*i).widget->SetPosition(
						sx + (spacing * x) + pos, sy,
						sx + (spacing * x) + pos + ww, sy + op);
				} else {
					(*i).widget->SetPosition(
						sx, sy + (spacing * x) + pos,
						sx + op, sy + (spacing * x) + pos + ww);
				}

				pos += ww;
			}
		}

		return;
	}

	/* Otherwise, we can more than fit our widgets...
	 * So the first order of business, find out how many are set to expand,
	 * and how much slush space we have to give them */
	// fprintf(stderr, "debug - %p we can fit all preferred\n", this);
	int bucket = 0;
	int num_fill = 0;
	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		int pp;

		if ((*i).widget->GetVisible() == 0) 
			continue;

		if (packing == 0) {
			pp = (*i).widget->GetPrefX();
		} else {
			pp = (*i).widget->GetPrefY();
		}

		/* Add up all the ones which aren't expanding to let us know
		 * how much we can give to the ones we can give more to */
		if ((*i).fill == 0) {
			bucket += pp;
		} else {
			num_fill++;
		}
	}

	// Reclaim our variable - our free bucket is the remainder of unclaimed 
	// stuff
	bucket = size - bucket - (spacing * (packed_items.size() - 1));
	// fprintf(stderr, "debug - %p bucket %d fill %d\n", this, bucket, num_fill);

	// Distribute the bucket over the expandable widgets, position, and draw
	pos = 0;
	i = packed_items.begin();
	for (int x = 0; i != packed_items.end(); ++i, x++) {
		int pp, op;

		if ((*i).widget->GetVisible() == 0)
			continue;

		if (packing == 0) {
			pp = (*i).widget->GetPrefX();
			/*
			op = (*i).widget->GetPrefY();
			if (op > ly || op == 0)
				op = ly;
				*/
			op = ly;
		} else {
			pp = (*i).widget->GetPrefY();
			/*
			op = (*i).widget->GetPrefX();
			if (op > lx || op == 0)
				op = lx;
				*/
			op = lx;
		}

		// Disperse the bucket over the items we have left
		if ((*i).fill != 0 && num_fill != 0) {
			pp = bucket / num_fill;
			bucket = bucket - pp;
			num_fill--;
		}

		if (packing == 0) {
			(*i).widget->SetPosition(
						 sx + pos, sy,
						 sx + pos + pp, sy + op);
		} else {
			(*i).widget->SetPosition(
						 sx, sy + pos, sx + op, sy + pos + pp);
		} 

		pos += pp + spacing;
	}
}

void Kis_Panel_Packbox::DrawComponent() {
	list<Kis_Panel_Packbox::packbox_details>::iterator i;

	if (visible == 0)
		return;

	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		if ((*i).widget->GetLayoutDirty()) {
			layout_dirty = 1;
			break;
		}
	}

	if (layout_dirty) {
		Pack_Widgets();
		layout_dirty = 0;
	}

	for (i = packed_items.begin(); i != packed_items.end(); ++i) {
		(*i).widget->DrawComponent();
		(*i).widget->SetLayoutDirty(0);
	}
}

Kis_Menu::Kis_Menu(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	cur_menu = -1;
	cur_item = -1;
	sub_item = -1;
	sub_menu = -1;
	mouse_triggered = 0;
	menuwin = NULL;
	submenuwin = NULL;
	text_color = border_color = 0;
}

Kis_Menu::~Kis_Menu() {
	ClearMenus();

	if (menuwin != NULL)
		delwin(menuwin);
	if (submenuwin != NULL)
		delwin(submenuwin);
}

int Kis_Menu::AddMenu(string in_text, int targ_char) {
	_menu *menu = new _menu;

	menu->text = in_text;
	if (targ_char < 0 || targ_char > (int) in_text.length() - 1)
		menu->targchar = -1;
	else
		menu->targchar = targ_char;

	menu->width = 0;

	menu->id = menubar.size();

	menu->submenu = 0;
	menu->visible = 1;
	menu->checked = -1;

	menubar.push_back(menu);

	return menu->id;
}

void Kis_Menu::SetMenuVis(int in_menu, int in_vis) {
	if (in_menu < 0 || in_menu > (int) menubar.size() - 1)
		return;

	menubar[in_menu]->visible = in_vis;
}

int Kis_Menu::AddMenuItem(string in_text, int menuid, char extra) {
	if (menuid < 0 || menuid > (int) menubar.size() - 1)
		return -1;

	_menuitem *item = new _menuitem;

	item->parentmenu = menuid;
	item->text = in_text;
	item->extrachar = extra;
	item->id = menubar[menuid]->items.size();
	item->visible = 1;
	item->checked = -1;
	item->colorpair = -1;

	// Auto-disable spacers
	if (item->text[0] != '-')
		item->enabled = 1;
	else
		item->enabled = 0;

	item->submenu = -1;

	menubar[menuid]->items.push_back(item);

	if ((int) in_text.length() > menubar[menuid]->width)
		menubar[menuid]->width = in_text.length();

	return (menuid * 100) + item->id + 1;
}

void Kis_Menu::SetMenuItemChecked(int in_item, int in_checked) {
	int mid = in_item / 100;
	int iid = (in_item % 100) - 1;

	if (mid < 0 || mid >= (int) menubar.size())
		return;

	if (iid < 0 || iid > (int) menubar[mid]->items.size())
		return;

	menubar[mid]->items[iid]->checked = in_checked;
	menubar[mid]->checked = -1;

	// Update the checked menu status
	for (unsigned int x = 0; x < menubar[mid]->items.size(); x++) {
		if (menubar[mid]->items[x]->checked > menubar[mid]->checked)
			menubar[mid]->checked = menubar[mid]->items[x]->checked;
	}
}

void Kis_Menu::SetMenuItemColor(int in_item, string in_color) {
	int mid = in_item / 100;
	int iid = (in_item % 100) - 1;

	if (mid < 0 || mid >= (int) menubar.size())
		return;

	if (iid < 0 || iid > (int) menubar[mid]->items.size())
		return;

	menubar[mid]->items[iid]->colorpair = parent_panel->AddColor(in_color);
}

int Kis_Menu::AddSubMenuItem(string in_text, int menuid, char extra) {
	if (menuid < 0 || menuid > (int) menubar.size() - 1)
		return -1;

	// Add a new menu to the menu handling system, which gives us
	// rational IDs and such.
	int smenuid = AddMenu(in_text, 0);
	// Mark the new menu record as a submenu so it doesn't get drawn
	// in the menubar
	menubar[smenuid]->submenu = 1;

	// Add a menu item to the requested parent menu, and flag it as a submenu
	// pointing to our menuid so we can find it during drawing
	int sitem = AddMenuItem(in_text, menuid, extra);

	menubar[menuid]->items[(sitem % 100) - 1]->submenu = smenuid;

	// Return the id of the menu we made so we can add things to it
	return smenuid;
}

void Kis_Menu::DisableMenuItem(int in_item) {
	int mid = in_item / 100;
	int iid = (in_item % 100) - 1;

	if (mid < 0 || mid >= (int) menubar.size())
		return;

	if (iid < 0 || iid > (int) menubar[mid]->items.size())
		return;

	menubar[mid]->items[iid]->enabled = 0;
}

void Kis_Menu::EnableMenuItem(int in_item) {
	int mid = in_item / 100;
	int iid = (in_item % 100) - 1;

	if (mid < 0 || mid >= (int) menubar.size())
		return;

	if (iid < 0 || iid > (int) menubar[mid]->items.size())
		return;

	menubar[mid]->items[iid]->enabled = 1;
}

void Kis_Menu::SetMenuItemVis(int in_item, int in_vis) {
	int mid = in_item / 100;
	int iid = (in_item % 100) - 1;

	if (mid < 0 || mid >= (int) menubar.size())
		return;

	if (iid < 0 || iid > (int) menubar[mid]->items.size())
		return;

	menubar[mid]->items[iid]->visible = in_vis;
}

void Kis_Menu::ClearMenus() {
	// Deconstruct the menubar
	for (unsigned int x = 0; x < menubar.size(); x++) {
		for (unsigned int y = 0; y < menubar[x]->items.size(); y++)
			delete menubar[x]->items[y];
		delete menubar[x];
	}
}

void Kis_Menu::Activate(int subcomponent) {
	Kis_Panel_Component::Activate(subcomponent);

	cur_menu = subcomponent - 1;
	cur_item = -1;
	sub_menu = -1;
	sub_item = -1;
}

void Kis_Menu::Deactivate() {
	Kis_Panel_Component::Deactivate();

	cur_menu = -1;
	cur_item = -1;
	sub_menu = -1;
	sub_item = -1;
	mouse_triggered = 0;
}

void Kis_Menu::DrawMenu(_menu *menu, WINDOW *win, int hpos, int vpos) {
	_menu *submenu = NULL;
	int subvpos = -1;
	int subhpos = -1;
	int dsz = 0;

	// Resize the menu window, taking invisible items into account.
	for (unsigned int y = 0; y < menu->items.size(); y++) {
		if (menu->items[y]->visible)
			dsz++;
	}

	wresize(win, dsz + 2, menu->width + 7);

	// move it
	mvderwin(win, vpos, hpos);

	// Draw the box
	wattrset(win, border_color);
	box(win, 0, 0);

	// Use dsz as the position to draw into
	dsz = 0;
	for (unsigned int y = 0; y < menu->items.size(); y++) {
		string menuline;

		if (menu->items[y]->visible == 0)
			continue;

		// Shortcut out a spacer
		if (menu->items[y]->text[0] == '-') {
			wattrset(win, border_color);
			mvwhline(win, 1 + dsz, 1, ACS_HLINE, menu->width + 5);
			mvwaddch(win, 1 + dsz, 0, ACS_LTEE);
			mvwaddch(win, 1 + dsz, menu->width + 6, ACS_RTEE);
			dsz++;
			continue;
		}

		wattrset(win, text_color);

		if (menu->items[y]->colorpair != -1)
			wattrset(win, menu->items[y]->colorpair);

		// Hilight the current item
		if (((int) menu->id == cur_menu && (int) y == cur_item) || 
			((int) menu->id == sub_menu && (int) y == sub_item))
			wattron(win, WA_REVERSE);

		// Draw the check 
		if (menu->items[y]->checked == 1) {
			menuline += "X ";
		} else if (menu->items[y]->checked == 0 || menu->checked > -1) {
			menuline += "  ";
		}

		// Dim a disabled item
		if (menu->items[y]->enabled == 0) {
			wattrset(win, disable_color);
		}

		// Format it with 'Foo     F'
		menuline += menu->items[y]->text + " ";
		for (unsigned int z = menuline.length(); 
			 (int) z <= menu->width + 2; z++) {
			menuline = menuline + string(" ");
		}

		if (menu->items[y]->submenu != -1) {
			menuline = menuline + ">>";

			// Draw again, using our submenu, if it's active
			if (menu->items[y]->submenu == cur_menu) {
				submenu = menubar[menu->items[y]->submenu];
				subvpos = vpos + dsz;
				subhpos = hpos + menu->width + 6;

			}
		} else if (menu->items[y]->extrachar != 0) {
			menuline = menuline + " " + menu->items[y]->extrachar;
		} else {
			menuline = menuline + "  ";
		}

		// Print it
		mvwaddstr(win, 1 + dsz, 1, menuline.c_str());

		if (((int) menu->id == cur_menu && (int) y == cur_item) || 
			((int) menu->id == sub_menu && (int) y == sub_item))
			wattroff(win, WA_REVERSE);

		dsz++;
	}

	// Draw the expanded submenu
	if (subvpos > 0 && subhpos > 0) {
		if (submenuwin == NULL)
			submenuwin = derwin(window, 1, 1, 0, 0);

		DrawMenu(submenu, submenuwin, subhpos, subvpos);
	}
}

void Kis_Menu::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("menu_text_color", "white,blue");
	parent_panel->InitColorPref("menu_border_color", "cyan,blue");
	parent_panel->InitColorPref("menu_disable_color", "grey,blue");
	parent_panel->ColorFromPref(text_color, "menu_text_color");
	parent_panel->ColorFromPref(border_color, "menu_border_color");
	parent_panel->ColorFromPref(disable_color, "menu_disable_color");

	int hpos = 3;

	if (menuwin == NULL)
		menuwin = derwin(window, 1, 1, 0, 0);

	wattron(window, border_color);
	mvwaddstr(window, sy, sx + 1, "~ ");

	// Draw the menu bar itself
	for (unsigned int x = 0; x < menubar.size(); x++) {
		if (menubar[x]->submenu || menubar[x]->visible == 0)
			continue;

		wattron(window, text_color);

		// If the current menu is the selected one, hilight it
		if ((int) x == cur_menu || (int) x == sub_menu)
			wattron(window, WA_REVERSE);

		// Draw the menu
		mvwaddstr(window, sy, sx + hpos, (menubar[x]->text).c_str());
		// Set the hilight
		if (menubar[x]->targchar >= 0) {
			wattron(window, WA_UNDERLINE);
			mvwaddch(window, sy, sx + hpos + menubar[x]->targchar,
					 menubar[x]->text[menubar[x]->targchar]);
			wattroff(window, WA_UNDERLINE);
		}

		wattroff(window, WA_REVERSE);

		mvwaddstr(window, sy, sx + hpos + menubar[x]->text.length(), " ");

		// Draw the menu itself, if we've got an item selected in it
		if (((int) x == cur_menu || (int) x == sub_menu) && 
			(sub_item >= 0 || cur_item >= 0 || mouse_triggered)) {
			DrawMenu(menubar[x], menuwin, sx + hpos, sy + 1);
		}

		hpos += menubar[x]->text.length() + 1;
	}
	wattroff(window, text_color);
}

void Kis_Menu::FindNextEnabledItem() {
	// Handle disabled and spacer items
	if (menubar[cur_menu]->items[cur_item]->enabled == 0) {
		// find the next enabled item
		for (int i = cur_item; i <= (int) menubar[cur_menu]->items.size(); i++) {
			// Loop
			if (i >= (int) menubar[cur_menu]->items.size())
				i = 0;

			if (menubar[cur_menu]->items[i]->visible == 0)
				continue;

			if (menubar[cur_menu]->items[i]->enabled) {
				cur_item = i;
				break;
			}
		}
	}
}

void Kis_Menu::FindPrevEnabledItem() {
	// Handle disabled and spacer items
	if (menubar[cur_menu]->items[cur_item]->enabled == 0) {
		// find the next enabled item
		for (int i = cur_item; i >= -1; i--) {
			// Loop
			if (i < 0)
				i = menubar[cur_menu]->items.size() - 1;

			if (menubar[cur_menu]->items[i]->visible == 0)
				continue;

			if (menubar[cur_menu]->items[i]->enabled) {
				cur_item = i;
				break;
			}
		}
	}
}

int Kis_Menu::KeyPress(int in_key) {
	if (visible == 0)
		return -1;

	// Activate menu
	if (in_key == '~' || in_key == '`' || in_key == 0x1B) {
		if (cur_menu < 0) {
			Activate(1);
		} else {
			// Break out of submenus 
			if (sub_menu != -1) {
				cur_menu = sub_menu;
				cur_item = sub_item;
				sub_menu = sub_item = -1;
				return 0;
			}

			Deactivate();
		}

		// We consume it but the framework doesn't get a state change
		return -1;
	}

	// Menu movement
	if (in_key == KEY_RIGHT && cur_menu >= 0) {

		// Break out of submenus on l/r
		if (sub_menu != -1) {
			cur_menu = sub_menu;
			cur_item = sub_item;
			sub_menu = sub_item = -1;
			return -1;
		}

		for (unsigned int nm = cur_menu + 1; nm < menubar.size(); nm++) {
			if (menubar[nm]->submenu == 0) {
				cur_menu = nm;
				cur_item = 0;
				FindNextEnabledItem();
				break;
			}
		}
			
		return -1;
	}

	if (in_key == KEY_LEFT && cur_menu > 0) {
		// Break out of submenus on l/r
		if (sub_menu != -1) {
			cur_menu = sub_menu;
			cur_item = sub_item;
			sub_menu = sub_item = -1;
			return -1;
		}

		for (int nm = cur_menu - 1; nm >= 0; nm--) {
			if (menubar[nm]->submenu == 0) {
				cur_menu = nm;
				cur_item = 0;
				FindNextEnabledItem();
				break;
			}
		}

		return -1;
	}

	if (in_key == KEY_DOWN && cur_menu >= 0 &&
		cur_item <= (int) menubar[cur_menu]->items.size() - 1) {

		if (cur_item == (int) menubar[cur_menu]->items.size() - 1) {
			cur_item = 0;
			FindNextEnabledItem();
			return -1;
		}

		cur_item++;

		FindNextEnabledItem();

		return -1;
	}

	if (in_key == KEY_UP && cur_item >= 0) {
		if (cur_item == 0) {
			cur_item = menubar[cur_menu]->items.size() - 1;
			FindPrevEnabledItem();
			return -1;
		}

		cur_item--;

		FindPrevEnabledItem();

		return -1;
	}

	// Space or enter
	if ((in_key == ' ' || in_key == 0x0A || in_key == KEY_ENTER) && cur_menu >= 0) {
		if (cur_item == -1) {
			cur_item = 0;
			FindNextEnabledItem();
			return -1;
		}

		// Are we entering a submenu?
		if (sub_menu == -1 && menubar[cur_menu]->items[cur_item]->submenu != -1) {
			// Remember where we were
			sub_menu = cur_menu;
			sub_item = cur_item;
			cur_menu = menubar[cur_menu]->items[cur_item]->submenu;
			cur_item = 0;
			return -1;
		}

		int ret = (cur_menu * 100) + cur_item + 1;
		Deactivate();
		
		if (cb_activate != NULL) 
			(*cb_activate)(this, ret, cb_activate_aux, globalreg);

		return ret;
	}

	// Key shortcuts
	if (cur_menu >= 0) {
		if (cur_item < 0) {
			// Try w/ the proper case
			for (unsigned int x = 0; x < menubar.size(); x++) {
				if (in_key == menubar[x]->text[menubar[x]->targchar]) {
					cur_menu = x;
					cur_item = 0;
					FindNextEnabledItem();
					return -1;
				}
			}
			// Try with lowercase, if we didn't find one already
			for (unsigned int x = 0; x < menubar.size(); x++) {
				if (tolower(in_key) == 
					tolower(menubar[x]->text[menubar[x]->targchar])) {
					cur_menu = x;
					cur_item = 0;
					FindNextEnabledItem();
					return -1;
				}
			}
			return -1;
		} else {
			for (unsigned int x = 0; x < menubar[cur_menu]->items.size(); x++) {
				if (in_key == menubar[cur_menu]->items[x]->extrachar &&
					menubar[cur_menu]->items[x]->enabled == 1) {
					int ret = (cur_menu * 100) + x + 1;
					Deactivate();
					if (cb_activate != NULL) 
						(*cb_activate)(this, ret, cb_activate_aux, globalreg);
					return ret;
				}
			}
			return -1;
		}
	}

	return 0;
}

int Kis_Menu::MouseEvent(MEVENT *mevent) {
	if (mevent->bstate == 4 && mevent->y == sy) {
		// Click happened somewhere in the menubar
		int hpos = 3;
		for (unsigned int x = 0; x < menubar.size(); x++) {
			if (menubar[x]->submenu || menubar[x]->visible == 0)
				continue;

			if (mevent->x < hpos)
				break;

			if (mevent->x <= hpos + (int) menubar[x]->text.length()) {
				if ((int) x == cur_menu) {
					Deactivate();
					// Consume w/ no state change to caller
					return -1;
				} else {
					Activate(0);
					cur_menu = x;
					cur_item = -1;
					mouse_triggered = 1;
					// Consume w/ no state change to caller
					return -1;
				}
			}

			hpos += menubar[x]->text.length() + 1;
		} /* menu list */
	} else if (mevent->bstate == 4 && mevent->y > sy && cur_menu >= 0) {
		int hpos = 3;
		int ypos = sy + 2;
		for (unsigned int x = 0; x < (unsigned int) (cur_menu + 1); x++) {
			if (menubar[x]->submenu || menubar[x]->visible == 0)
				continue;

			if (mevent->x < hpos)
				break;

			hpos += menubar[x]->text.length() + 1;

			if (mevent->x <= hpos + (int) menubar[x]->text.length()) {
				for (unsigned int y = 0; y < menubar[x]->items.size(); y++) {
					if (menubar[x]->items[y]->visible == 0)
						continue;
					 
					if (menubar[x]->items[y]->text[0] == '-') {
						ypos++;
						continue;
					}

					if (ypos == mevent->y) {
						if (menubar[x]->items[y]->enabled == 0)
							return -1;

						// Trigger the item
						if (cb_activate != NULL)
							(*cb_activate)(this, (cur_menu * 100) + y + 1,
										   cb_activate_aux, globalreg);

						Deactivate();

						return (cur_menu * 100) + y + 1;
					}

					ypos++;
				}
			}
		}
	}

	return 0;
}

Kis_Pop_Menu::Kis_Pop_Menu(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Menu(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	cur_menu = -1;
	cur_item = -1;
	sub_item = -1;
	sub_menu = -1;
	menuwin = NULL;
	submenuwin = NULL;
	text_color = border_color = 0;
}

Kis_Pop_Menu::~Kis_Pop_Menu() {
	// The parent deconstructor handles clearing menus
}

int Kis_Pop_Menu::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	if ((in_key == 0x0A || in_key == 0x0A) && cur_menu < 0) {
		Activate(1);
		cur_item = 0;
		FindNextEnabledItem();
		return -1;
	}

	int ret = Kis_Menu::KeyPress(in_key);

	// Update the menu selection if we picked something
	if (cur_menu < 0) {
		Activate(1);
		cur_item = 0;
		FindNextEnabledItem();
	}

	return ret;
}

void Kis_Pop_Menu::DrawMenu(_menu *menu, WINDOW *win, int hpos, int vpos) {
	_menu *submenu = NULL;
	int subvpos = -1;
	int subhpos = -1;
	int dsz = 0;
	int scrollable = 0;

	// Resize the menu window, taking invisible items into account.
	for (unsigned int y = 0; y < menu->items.size(); y++) {
		if (menu->items[y]->visible)
			dsz++;
	}

	// Try to size it max, but stay w/in the window constraints
	if (dsz > parent_panel->FetchSzy() - 6) {
		dsz = parent_panel->FetchSzy() - 6;
		scrollable = 1;
	}

	// Position it - hpos and vpos are passed as the position of the menu, so
	// we can fit it inside the window w/in those constraints
	if (scrollable) {
		// If we're scrollable, then we're maxed out on size anyhow, so we
		// set the vpos directly
		vpos = 2;
	} else {
		// We can fit in the panel w/out scrolling, so div the height in half
		// and position it relative to where we started
		vpos -= (dsz / 2);
	}

	if (hpos + menu->width + 7 > parent_panel->FetchSzx()) {
		// If we can't fit in before the end of the window, slide the menu over
		hpos -= (parent_panel->FetchSzx() - (menu->width + 7));
	}

	wresize(win, dsz + 2, menu->width + 7);

	// move it
	mvderwin(win, vpos, hpos);

	// Draw the box
	wattrset(win, border_color);
	box(win, 0, 0);

	// Use dsz as the position to draw into
	dsz = 0;
	for (unsigned int y = 0; y < menu->items.size(); y++) {
		string menuline;

		if (menu->items[y]->visible == 0)
			continue;

		// Shortcut out a spacer
		if (menu->items[y]->text[0] == '-') {
			wattrset(win, border_color);
			mvwhline(win, 1 + dsz, 1, ACS_HLINE, menu->width + 5);
			mvwaddch(win, 1 + dsz, 0, ACS_LTEE);
			mvwaddch(win, 1 + dsz, menu->width + 6, ACS_RTEE);
			dsz++;
			continue;
		}

		wattrset(win, text_color);

		if (menu->items[y]->colorpair != -1)
			wattrset(win, menu->items[y]->colorpair);

		// Hilight the current item
		if (((int) menu->id == cur_menu && (int) y == cur_item) || 
			((int) menu->id == sub_menu && (int) y == sub_item))
			wattron(win, WA_REVERSE);

		// Draw the check 
		if (menu->items[y]->checked == 1) {
			menuline += "X ";
		} else if (menu->items[y]->checked == 0 || menu->checked > -1) {
			menuline += "  ";
		}

		// Dim a disabled item
		if (menu->items[y]->enabled == 0)
			wattron(win, WA_DIM);

		// Format it with 'Foo     F'
		menuline += menu->items[y]->text + " ";
		for (unsigned int z = menuline.length(); 
			 (int) z <= menu->width + 2; z++) {
			menuline = menuline + string(" ");
		}

		if (menu->items[y]->submenu != -1) {
			menuline = menuline + ">>";

			// Draw again, using our submenu, if it's active
			if (menu->items[y]->submenu == cur_menu) {
				submenu = menubar[menu->items[y]->submenu];
				subvpos = vpos + dsz;
				subhpos = hpos + menu->width + 6;

			}
		} else if (menu->items[y]->extrachar != 0) {
			menuline = menuline + " " + menu->items[y]->extrachar;
		} else {
			menuline = menuline + "  ";
		}

		// Print it
		mvwaddstr(win, 1 + dsz, 1, menuline.c_str());

		// Dim a disabled item
		if (menu->items[y]->enabled == 0)
			wattroff(win, WA_DIM);

		if (((int) menu->id == cur_menu && (int) y == cur_item) || 
			((int) menu->id == sub_menu && (int) y == sub_item))
			wattroff(win, WA_REVERSE);

		dsz++;
	}

	// Draw the expanded submenu
	if (subvpos > 0 && subhpos > 0) {
		if (submenuwin == NULL)
			submenuwin = derwin(window, 1, 1, 0, 0);

		DrawMenu(submenu, submenuwin, subhpos, subvpos);
	}
}

void Kis_Pop_Menu::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("menu_text_color", "white,blue");
	parent_panel->InitColorPref("menu_border_color", "cyan,blue");
	parent_panel->ColorFromPref(text_color, "menu_text_color");
	parent_panel->ColorFromPref(border_color, "menu_border_color");

	if (menuwin == NULL)
		menuwin = derwin(window, 1, 1, 0, 0);

	wattron(window, border_color);

	// Draw the menu item itself
	if (menubar.size() == 0)
		return;

	if (cur_menu)
		wattron(window, WA_REVERSE);

	// Draw the menu
	mvwaddstr(window, sy, sx, string(menubar[0]->text + " V").c_str());

	if (cur_menu)
		wattroff(window, WA_REVERSE);

	// Draw the menu itself, if we've got an item selected in it
	if (cur_menu == 0 && (sub_item >= 0 || cur_item >= 0)) {
		DrawMenu(menubar[0], menuwin, sx, sy);
	}
}

Kis_Free_Text::Kis_Free_Text(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	scroll_pos = 0;
	SetMinSize(1, 1);
	alignment = 0;
}

Kis_Free_Text::~Kis_Free_Text() {
	// Nothing
}

void Kis_Free_Text::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	if (scroll_pos < 0 || scroll_pos > (int) text_vec.size())
		scroll_pos = 0;

	for (unsigned int x = 0; x < text_vec.size() && (int) x < ly; x++) {
		if (x + scroll_pos > text_vec.size()) {
			scroll_pos = x;
		}
		// Use the special formatter
		Kis_Panel_Specialtext::Mvwaddnstr(window, sy + x, sx, 
										  text_vec[x + scroll_pos],
										  lx - 1);
	}

	if ((int) text_vec.size() > ly) {
		// Draw the hash scroll bar
		mvwvline(window, sy, sx + lx - 1, ACS_VLINE, ly);
		// Figure out how far down our text we are
		// int perc = ey * (scroll_pos / text_vec.size());
		float perc = (float) ly * (float) ((float) (scroll_pos) / 
										   (float) (text_vec.size() - ly));
		wattron(window, WA_REVERSE);
		// Draw the solid position
		mvwaddch(window, sy + (int) perc, sx + lx - 1, ACS_BLOCK);

		wattroff(window, WA_REVERSE);
	}
}

int Kis_Free_Text::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	int scrollable = 1;

	if ((int) text_vec.size() <= ey)
		scrollable = 0;

	if (scrollable && in_key == KEY_UP && scroll_pos > 0) {
		scroll_pos--;
		return 0;
	}

	if (scrollable && in_key == KEY_DOWN && 
		scroll_pos < ((int) text_vec.size() - ey)) {
		scroll_pos++;
		return 0;
	}

	if (scrollable && in_key == KEY_PPAGE && scroll_pos > 0) {
		scroll_pos -= (ey - 1);
		if (scroll_pos < 0)
			scroll_pos = 0;
		return 0;
	}

	if (scrollable && in_key == KEY_NPAGE) {
		scroll_pos += (ey - 1);
		if (scroll_pos >= ((int) text_vec.size() - ey)) 
			scroll_pos = ((int) text_vec.size() - ey);
		return 0;
	}

	return 1;
}

void Kis_Free_Text::SetText(string in_text) {
	text_vec = StrTokenize(in_text, "\n");
	SetPreferredSize(Kis_Panel_Specialtext::Strlen(in_text), 1);
}

void Kis_Free_Text::SetText(vector<string> in_text) {
	unsigned int ml = 0;

	for (unsigned x = 0; x < in_text.size(); x++) {
		if (Kis_Panel_Specialtext::Strlen(in_text[x]) > ml) 
			ml = Kis_Panel_Specialtext::Strlen(in_text[x]);
	}

	text_vec = in_text;

	SetPreferredSize(ml, in_text.size());
}

void KisStatusText_Messageclient::ProcessMessage(string in_msg, int in_flags) {
	if ((in_flags & MSGFLAG_INFO)) {
		((Kis_Status_Text *) auxptr)->AddLine("\004bINFO\004B: " + in_msg, 6);
	} else if ((in_flags & MSGFLAG_ERROR)) {
		((Kis_Status_Text *) auxptr)->AddLine("\004rERROR\004R: " + in_msg, 7);
	} else if ((in_flags & MSGFLAG_FATAL)) {
		((Kis_Status_Text *) auxptr)->AddLine("\004rFATAL\004R: " + in_msg, 7);
	} else {
		((Kis_Status_Text *) auxptr)->AddLine(in_msg);
	}
}

Kis_Status_Text::Kis_Status_Text(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	scroll_pos = 0;
	status_color_normal = -1;
}

Kis_Status_Text::~Kis_Status_Text() {
	// Nothing
}

void Kis_Status_Text::DrawComponent() {
	parent_panel->InitColorPref("status_normal_color", "white,black");
	parent_panel->ColorFromPref(status_color_normal, "status_normal_color");

	if (visible == 0)
		return;

	wattrset(window, status_color_normal);

	for (unsigned int x = 0; x < text_vec.size() && (int) x < ly; x++) {
		Kis_Panel_Specialtext::Mvwaddnstr(window, ey - x, sx,
										  text_vec[text_vec.size() - x - 1],
										  ex - 1);
	}
}

int Kis_Status_Text::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	return 1;
}

void Kis_Status_Text::AddLine(string in_line, int headeroffset) {
	vector<string> lw = LineWrap(in_line, headeroffset, ex - 1);

	for (unsigned int x = 0; x < lw.size(); x++) {
		text_vec.push_back(lw[x]);
	}

	if ((int) text_vec.size() > py) {
		text_vec.erase(text_vec.begin(), text_vec.begin() + text_vec.size() - py);
	}
}

Kis_Field_List::Kis_Field_List(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	scroll_pos = 0;
	field_w = 0;
}

Kis_Field_List::~Kis_Field_List() {
	// Nothing
}

void Kis_Field_List::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	for (unsigned int x = 0; x < field_vec.size() && (int) x < ey; x++) {
		// Set the field name to bold
		wattron(window, WA_UNDERLINE);
		mvwaddnstr(window, sy + x, sx, field_vec[x + scroll_pos].c_str(), field_w);
		mvwaddch(window, sy + x, sx + field_w, ':');
		wattroff(window, WA_UNDERLINE);

		// Draw the data, leave room on the end for the scrollbar
		mvwaddnstr(window, sy + x, sx + field_w + 2, data_vec[x + scroll_pos].c_str(),
				   sx - field_w - 3);
	}

	if ((int) field_vec.size() > ey) {
		// Draw the hash scroll bar
		mvwvline(window, sy, sx + ex - 1, ACS_VLINE, ey);
		// Figure out how far down our text we are
		// int perc = ey * (scroll_pos / text_vec.size());
		float perc = (float) ey * (float) ((float) (scroll_pos) / 
										   (float) (field_vec.size() - ey));
		wattron(window, WA_REVERSE);
		// Draw the solid position
		mvwaddch(window, sy + (int) perc, sx + ex - 1, ACS_BLOCK);

		wattroff(window, WA_REVERSE);
	}
}

int Kis_Field_List::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	int scrollable = 1;

	if ((int) field_vec.size() <= ey)
		scrollable = 0;

	if (scrollable && in_key == KEY_UP && scroll_pos > 0) {
		scroll_pos--;
		return 0;
	}

	if (scrollable && in_key == KEY_DOWN && 
		scroll_pos < ((int) field_vec.size() - ey)) {
		scroll_pos++;
		return 0;
	}

	if (scrollable && in_key == KEY_PPAGE && scroll_pos > 0) {
		scroll_pos -= (ey - 1);
		if (scroll_pos < 0)
			scroll_pos = 0;
		return 0;
	}

	if (scrollable && in_key == KEY_NPAGE) {
		scroll_pos += (ey - 1);
		if (scroll_pos >= ((int) field_vec.size() - ey)) 
			scroll_pos = ((int) field_vec.size() - ey);
		return 0;
	}

	return 1;
}

int Kis_Field_List::AddData(string in_field, string in_data) {
	int pos = field_vec.size();
	field_vec.push_back(in_field);
	data_vec.push_back(in_data);

	if (in_field.length() > field_w)
		field_w = in_field.length();

	return (int) pos;
}

int Kis_Field_List::ModData(unsigned int in_row, string in_field, string in_data) {
	if (in_row >= field_vec.size())
		return -1;

	field_vec[in_row] = in_field;
	data_vec[in_row] = in_data;

	return (int) in_row;
}

Kis_Scrollable_Table::Kis_Scrollable_Table(GlobalRegistry *in_globalreg, 
										   Kis_Panel *in_panel) : 
	Kis_Panel_Component(in_globalreg, in_panel) {

	globalreg = in_globalreg;

	scroll_pos = 0;
	hscroll_pos = 0;
	selected = -1;

	SetMinSize(0, 3);

	draw_lock_scroll_top = 0;
	draw_highlight_selected = 1;
	draw_titles = 1;
}

Kis_Scrollable_Table::~Kis_Scrollable_Table() {
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		delete data_vec[x];
	}
}

void Kis_Scrollable_Table::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	// Current character position x
	int xcur = 0;
	int ycur = 0;
	string ftxt;

	// Assign widths to '0' sized things by dividing what's left
	// into them.  We'll assume the caller doesn't generate a horizontally
	// scrollable table with variable width fields.
	int ndynf = 0, spare = lx;
	for (unsigned int x = 0; x < title_vec.size(); x++) {
		title_vec[x].draw_width = title_vec[x].width;

		if (title_vec[x].width < 0)
			continue;

		if (title_vec[x].width == 0) {
			ndynf++;
			continue;
		}

		spare -= (title_vec[x].draw_width + 1);
	}
	// Distribute the spare over the rest
	if (spare > 0) {
		for (unsigned int x = 0; x < title_vec.size() && ndynf > 0; x++) {
			if (title_vec[x].width == 0) {
				title_vec[x].draw_width = spare / ndynf;
				spare -= spare / ndynf--;
			}
		}
	}

	// Print across the titles
	if (draw_titles) {
		wattron(window, WA_UNDERLINE);
		for (unsigned int x = hscroll_pos; x < title_vec.size() && xcur < lx; x++) {

			int w = title_vec[x].draw_width;

			if (xcur + w >= ex)
				w = lx - xcur;

			// Align the field w/in the width
			ftxt = AlignString(title_vec[x].title, ' ', title_vec[x].alignment, w);

			// Write it out
			mvwaddstr(window, sy, sx + xcur, ftxt.c_str());

			// Advance by the width + 1
			xcur += w + 1;
		}
		wattroff(window, WA_UNDERLINE);
		ycur += 1;
	}

	if ((int) data_vec.size() > ly) {
		// Draw the scroll bar
		mvwvline(window, sy, sx + lx - 1, ACS_VLINE, ly);
		float perc = (float) ly * (float) ((float) (scroll_pos) /
										   (float) (data_vec.size() - ly));
		if (perc > ly - 1)
			perc = ly - 1;
		wattron(window, WA_REVERSE);
		mvwaddch(window, sy + (int) perc, sx + lx - 1, ACS_BLOCK);
		wattroff(window, WA_REVERSE);
	}

	// Jump to the scroll location to start drawing rows
	for (unsigned int r = scroll_pos ? scroll_pos : 0; 
		 r < data_vec.size() && ycur < ly; r++) {
		// Print across
		xcur = 0;

		if ((int) r == selected && draw_highlight_selected) {
			wattron(window, WA_REVERSE);
			mvwhline(window, sy + ycur, sx, ' ', lx);
		}

		for (unsigned int x = hscroll_pos; x < data_vec[r]->data.size() &&
			 xcur < lx && x < title_vec.size(); x++) {
			int w = title_vec[x].draw_width;

			if (xcur + w >= lx)
				w = lx - xcur;

			ftxt = AlignString(data_vec[r]->data[x], ' ', title_vec[x].alignment, w);

			mvwaddstr(window, sy + ycur, sx + xcur, ftxt.c_str());

			xcur += w + 1;
		}

		if ((int) r == selected && draw_highlight_selected)
			wattroff(window, WA_REVERSE);

		ycur += 1;

	}
}

int Kis_Scrollable_Table::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	int scrollable = 1;
	if ((int) data_vec.size() < ly)
		scrollable = 0;

	// Selected up one, scroll up one if we need to
	if (in_key == KEY_UP) {
		if (draw_highlight_selected == 0 && scrollable) {
			// If we're not drawing the highlights then we don't mess
			// with the selected item at all, we just slide the scroll
			// pos up and down, and make sure we don't let them scroll
			// off the end of the world, keep as much of the tail in view
			// as possible
			if (scroll_pos > 0)
				scroll_pos--;
		} else if (selected > 0) {
			selected--;
			if (scrollable && scroll_pos > 0 && scroll_pos > selected) {
				scroll_pos--;
			}
		}
	}

	if (in_key == KEY_DOWN && selected < (int) data_vec.size() - 1) {
		if (draw_highlight_selected == 0 && scrollable) {
			// If we're not drawing the highlights then we don't mess
			// with the selected item at all, we just slide the scroll
			// pos up and down, and make sure we don't let them scroll
			// off the end of the world, keep as much of the tail in view
			// as possible
			if (scroll_pos + ly <= (int) data_vec.size() - 1)
				scroll_pos++;
		} else if (draw_lock_scroll_top && scrollable &&
			scroll_pos + ly - 1 <= selected) {
			// If we're locked to always keep the list filled, we can only
			// scroll until the bottom is visible.  This implies we don't 
			// show the selected row, too
			selected++;
			scroll_pos++;
		} else {
			selected++;
			if (scrollable && scroll_pos + ly - 1 <= selected) {
				scroll_pos++;
			}
		}
	}

	if (in_key == KEY_RIGHT && hscroll_pos < (int) title_vec.size() - 1) {
		hscroll_pos++;
	}

	if (in_key == KEY_LEFT && hscroll_pos > 0) {
		hscroll_pos--;
	}

	if (in_key == '\n' || in_key == '\r' || in_key == ' ') {
		if (cb_activate != NULL) 
			(*cb_activate)(this, GetSelected(), cb_activate_aux, globalreg);

		return GetSelected();
	}

	return 0;
}

int Kis_Scrollable_Table::GetSelected() {
	if (selected >= 0 && selected < (int) data_vec.size()) {
		return data_vec[selected]->key;
	}

	return -1;
}

vector<string> Kis_Scrollable_Table::GetRow(int in_key) {
	vector<string> ret;

	if (in_key >= 0 && in_key < (int) data_vec.size()) {
		return data_vec[in_key]->data;
	}

	return ret;
}

vector<string> Kis_Scrollable_Table::GetSelectedData() {
	vector<string> ret;

	if (selected >= 0 && selected < (int) data_vec.size()) {
		return data_vec[selected]->data;
	}

	return ret;
}

int Kis_Scrollable_Table::SetSelected(int in_key) {
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		if (data_vec[x]->key == in_key) {
			selected = x;
			return 1;
		}
	}

	return 0;
}

int Kis_Scrollable_Table::AddTitles(vector<Kis_Scrollable_Table::title_data> 
									in_titles) {
	title_vec = in_titles;
	return 1;
}

int Kis_Scrollable_Table::AddRow(int in_key, vector<string> in_fields) {
	if (key_map.find(in_key) != key_map.end()) {
		_MSG("Scrollable_Table tried to add row already keyed", MSGFLAG_ERROR);
		return -1;
	}

	if (in_fields.size() != title_vec.size()) {
		_MSG("Scrollable_Table added row with a different number of fields than "
			 "the title", MSGFLAG_ERROR);
	}

	row_data *r = new row_data;
	r->key = in_key;
	r->data = in_fields;

	key_map[in_key] = 1;

	data_vec.push_back(r);

	SetPreferredSize(0, data_vec.size() + 2);

	return 1;
}

int Kis_Scrollable_Table::DelRow(int in_key) {
	if (key_map.find(in_key) == key_map.end()) {
		_MSG("Scrollable_Table tried to del row that doesn't exist", MSGFLAG_ERROR);
		return -1;
	}

	key_map.erase(key_map.find(in_key));
	
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		if (data_vec[x]->key == in_key) {
			delete data_vec[x];
			data_vec.erase(data_vec.begin() + x);
			break;
		}
	}

	if (scroll_pos >= (int) data_vec.size()) {
		scroll_pos = data_vec.size() - 1;
		if (scroll_pos < 0)
			scroll_pos = 0;
	}

	if (selected >= (int) data_vec.size()) {
		selected = data_vec.size() - 1;
	}

	return 1;
}

int Kis_Scrollable_Table::ReplaceRow(int in_key, vector<string> in_fields) {
	if (key_map.find(in_key) == key_map.end()) {
		// Add a row instead
		return AddRow(in_key, in_fields);

#if 0
		_MSG("Scrollable_Table tried to replace row that doesn't exist", 
			 MSGFLAG_ERROR);
		return -1;
#endif
	}

	for (unsigned int x = 0; x < data_vec.size(); x++) {
		if (data_vec[x]->key == in_key) {
			data_vec[x]->data = in_fields;
			break;
		}
	}

	return 1;
}

void Kis_Scrollable_Table::Clear() {
	for (unsigned int x = 0; x < data_vec.size(); x++) 
		delete data_vec[x];

	data_vec.clear();
	key_map.clear();

	return;
}

Kis_Single_Input::Kis_Single_Input(GlobalRegistry *in_globalreg, 
								   Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;
	curs_pos = 0;
	inp_pos = 0;
	label_pos = LABEL_POS_NONE;
	max_len = 0;
	draw_len = 0;
}

Kis_Single_Input::~Kis_Single_Input() {
	// Nothing
}

void Kis_Single_Input::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	int xoff = 0;
	int yoff = 0;

	// Draw the label if we can, in bold
	if (ly >= 2 && label_pos == LABEL_POS_TOP) {
		wattron(window, WA_BOLD);
		mvwaddnstr(window, sy, sx, label.c_str(), lx);
		wattroff(window, WA_BOLD);
		yoff = 1;
	} else if (label_pos == LABEL_POS_LEFT) {
		wattron(window, WA_BOLD);
		mvwaddnstr(window, sy, sx, label.c_str(), lx);
		wattroff(window, WA_BOLD);
		xoff += label.length() + 1;
	}

	// set the drawing length
	draw_len = lx - xoff;

	if (draw_len < 0)
		draw_len = 0;

	// Clean up any silliness that might be present from initialization
	if (inp_pos - curs_pos >= draw_len)
		curs_pos = inp_pos - draw_len + 1;

	// Invert for the text
	wattron(window, WA_REVERSE);

	/* draw the inverted line */
	mvwhline(window, sy + yoff, sx + xoff, ' ', draw_len);

	if (curs_pos >= (int) text.length())
		curs_pos = 0;

	// fprintf(stderr, "debug - about to try to substr %d %d from len %d\n", curs_pos, draw_len, text.length());

	/* draw the text from cur to what fits */
	mvwaddnstr(window, sy + yoff, sx + xoff, 
			   text.substr(curs_pos, draw_len).c_str(), draw_len);

	/* Underline & unreverse the last character of the text (or space) */
	wattroff(window, WA_REVERSE);

	if (active) {
		wattron(window, WA_UNDERLINE);
		char ch;
		if (inp_pos < (int) text.length())
			ch = text[inp_pos];
		else
			ch = ' ';

		mvwaddch(window, sy + yoff, sx + xoff + (inp_pos - curs_pos), ch);
		wattroff(window, WA_UNDERLINE);
	}
}

int Kis_Single_Input::KeyPress(int in_key) {
	if (visible == 0 || draw_len == 0)
		return 0;

	// scroll left, and move the viewing window if we have to
	if (in_key == KEY_LEFT && inp_pos > 0) {
		inp_pos--;
		if (inp_pos < curs_pos)
			curs_pos = inp_pos;
		return 0;
	}

	// scroll right, and move the viewing window if we have to
	if (in_key == KEY_RIGHT && inp_pos < (int) text.length()) {
		inp_pos++;

		if (inp_pos - curs_pos >= draw_len)
			curs_pos = inp_pos - draw_len + 1;

		return 0;
	}

	// Catch home/end (if we can)
	if (in_key == KEY_HOME) {
		inp_pos = 0;
		curs_pos = 0;

		return 0;
	}
	if (in_key == KEY_END) {
		inp_pos = text.length();
		curs_pos = inp_pos - draw_len + 1;

		return 0;
	}

	// Catch deletes
	if ((in_key == KEY_BACKSPACE || in_key == 0x7F) && text.length() > 0) {
		if (inp_pos == 0)
			inp_pos = 1;

		text.erase(text.begin() + (inp_pos - 1));

		if (inp_pos > 0)
			inp_pos--;

		if (inp_pos < curs_pos)
			curs_pos = inp_pos;

		return 0;
	}

	// Lastly, if the character is in our filter of allowed characters for typing,
	// and if we have room, insert it and scroll to the right
	if ((int) text.length() < max_len && 
		filter_map.find(in_key) != filter_map.end()) {
		char ins[2] = { in_key, 0 };
		text.insert(inp_pos, ins);
		inp_pos++;

		if (inp_pos - curs_pos >= draw_len)
			curs_pos = inp_pos - draw_len + 1;

		return 0;
	}

	return 0;
}

int Kis_Single_Input::MouseEvent(MEVENT *mevent) {
	int mwx, mwy;
	getbegyx(window, mwy, mwx);

	mwx = mevent->x - mwx;
	mwy = mevent->y - mwy;

	if (mevent->bstate == 4 && mwy == sy && mwx >= sx && mwx <= ex) {
		// Single input only does a focus switch on mouse events
		if (cb_switch != NULL) 
			(*cb_switch)(this, 1, cb_switch_aux, globalreg);

		return 1;
	}

	return 0;
}

void Kis_Single_Input::SetCharFilter(string in_charfilter) {
	filter_map.clear();
	for (unsigned int x = 0; x < in_charfilter.length(); x++) {
		filter_map[in_charfilter[x]] = 1;
	}
}

void Kis_Single_Input::SetLabel(string in_label, KisWidget_LabelPos in_pos) {
	label = in_label;
	label_pos = in_pos;
	SetPreferredSize(label.length() + max_len + 1, 1);
	SetMinSize(label.length() + 3, 1);
}

void Kis_Single_Input::SetTextLen(int in_len) {
	max_len = in_len;
	SetPreferredSize(in_len + label.length() + 1, 1);
	SetMinSize(label.length() + 3, 1);
}

void Kis_Single_Input::SetText(string in_text, int dpos, int ipos) {
	text = in_text;

	if (ipos < 0)
		inp_pos = in_text.length();
	else
		inp_pos = ipos;

	if (dpos < 0)
		curs_pos = 0;
	else
		curs_pos = dpos;
}

string Kis_Single_Input::GetText() {
	return text;
}

Kis_Button::Kis_Button(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;

	active = 0;
	SetMinSize(3, 1);
}

Kis_Button::~Kis_Button() {
	// nada
}

void Kis_Button::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	// Draw the highlighted button area if we're active
	if (active)
		wattron(window, WA_REVERSE);

	mvwhline(window, sy, sx, ' ', lx);

	// Center the text
	int tx = (lx / 2) - (text.length() / 2);
	mvwaddnstr(window, sy, sx + tx, text.c_str(), lx - tx);

	// Add the ticks 
	mvwaddch(window, sy, sx, '[');
	mvwaddch(window, sy, sx + lx - 1, ']');

	if (active)
		wattroff(window, WA_REVERSE);
}

int Kis_Button::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	if (in_key == KEY_ENTER || in_key == '\n' || in_key == ' ') {
		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 1;
	}

	return 0;
}

int Kis_Button::MouseEvent(MEVENT *mevent) {
	int mwx, mwy;
	getbegyx(window, mwy, mwx);

	mwx = mevent->x - mwx;
	mwy = mevent->y - mwy;

	if (mevent->bstate == 4 && mwy == sy && mwx >= sx && mwx <= ex) {
		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 1;
	}

	return 0;
}

void Kis_Button::SetText(string in_text) {
	text = in_text;
	SetPreferredSize(text.length() + 4, 1);
}

Kis_Checkbox::Kis_Checkbox(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;

	active = 0;
	checked = 0;
}

Kis_Checkbox::~Kis_Checkbox() {
	// nada
}

void Kis_Checkbox::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	// Draw the highlighted button area if we're active
	if (active)
		wattron(window, WA_REVERSE);

	mvwhline(window, sy, sx, ' ', lx);

	if (checked) {
		mvwaddnstr(window, sy, sx, "[X]", 3);
	} else {
		mvwaddnstr(window, sy, sx, "[ ]", 3);
	}

	mvwaddnstr(window, sy, sx + 4, text.c_str(), lx - 4);

	if (active)
		wattroff(window, WA_REVERSE);
}

void Kis_Checkbox::Activate(int subcomponent) {
	active = 1;
}

void Kis_Checkbox::Deactivate() {
	active = 0;
}

int Kis_Checkbox::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	if (in_key == KEY_ENTER || in_key == '\n' || in_key == ' ') {
		checked = !checked;

		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 0;
	}

	return 0;
}

int Kis_Checkbox::MouseEvent(MEVENT *mevent) {
	int mwx, mwy;
	getbegyx(window, mwy, mwx);

	mwx = mevent->x - mwx;
	mwy = mevent->y - mwy;

	if (mevent->bstate == 4 && mwy == sy && mwx >= sx && mwx <= ex) {
		checked = !checked;

		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 1;
	}

	return 0;
}

void Kis_Checkbox::SetText(string in_text) {
	text = in_text;
	SetPreferredSize(text.length() + 4, 1);
}

int Kis_Checkbox::GetChecked() {
	return checked;
}

void Kis_Checkbox::SetChecked(int in_check) {
	checked = in_check;
}

Kis_Radiobutton::Kis_Radiobutton(GlobalRegistry *in_globalreg, Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	globalreg = in_globalreg;

	active = 0;
	checked = 0;
}

Kis_Radiobutton::~Kis_Radiobutton() {
	// nada
}

void Kis_Radiobutton::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->InitColorPref("panel_textdis_color", "grey,black");
	parent_panel->ColorFromPref(color_active, "panel_text_color");
	parent_panel->ColorFromPref(color_inactive, "panel_textdis_color");

	SetTransColor(color_active);

	// Draw the highlighted button area if we're active
	if (active)
		wattron(window, WA_REVERSE);

	mvwhline(window, sy, sx, ' ', lx);

	if (checked) {
		mvwaddnstr(window, sy, sx, "(*)", 3);
	} else {
		mvwaddnstr(window, sy, sx, "( )", 3);
	}

	mvwaddnstr(window, sy, sx + 4, text.c_str(), lx - 4);

	if (active)
		wattroff(window, WA_REVERSE);
}

void Kis_Radiobutton::Activate(int subcomponent) {
	active = 1;
}

void Kis_Radiobutton::Deactivate() {
	active = 0;
}

int Kis_Radiobutton::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	if (in_key == KEY_ENTER || in_key == '\n' || in_key == ' ') {
		if (!checked)
			SetChecked(1);

		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 0;
	}

	return 0;
}

int Kis_Radiobutton::MouseEvent(MEVENT *mevent) {
	int mwx, mwy;
	getbegyx(window, mwy, mwx);

	mwx = mevent->x - mwx;
	mwy = mevent->y - mwy;

	if (mevent->bstate == 4 && mwy == sy && mwx >= sx && mwx <= ex) {
		if (!checked)
			SetChecked(1);

		if (cb_activate != NULL) 
			(*cb_activate)(this, 1, cb_activate_aux, globalreg);

		return 1;
	}

	return 0;
}

void Kis_Radiobutton::SetText(string in_text) {
	text = in_text;
	SetPreferredSize(text.length() + 4, 1);
}

int Kis_Radiobutton::GetChecked() {
	return checked;
}

void Kis_Radiobutton::SetChecked(int in_check) {
	checked = in_check;

	for (unsigned int x = 0; x < linked_vec.size() && in_check; x++) 
		linked_vec[x]->SetChecked(0);

}

void Kis_Radiobutton::LinkRadiobutton(Kis_Radiobutton *in_button) {
	linked_vec.push_back(in_button);
}

void Kis_IntGraph::AddExtDataVec(string name, int layer, string colorpref,  
								 string colordefault, char line, char fill, 
								 int overunder, vector<int> *in_dv) {
	graph_source gs;

	gs.layer = layer;
	gs.colorpref = colorpref;
	gs.colordefault = colordefault;
	gs.colorval = 0;
	snprintf(gs.line, 2, "%c", line);
	snprintf(gs.fill, 2, "%c", fill);
	gs.data = in_dv;
	gs.name = name;
	gs.overunder = overunder;

	// Can't figure out how to do a sort template of a template class, so hell
	// with it, we'll do it sloppily here.  Assembled least to greatest priority
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		if (layer < data_vec[x].layer) {
			data_vec.insert(data_vec.begin() + x, gs);
			return;
		}
	}

	if (name.length() > maxlabel) 
		maxlabel = name.length();
	
	// Add it to the end otherwise
	data_vec.push_back(gs);
}

int Kis_IntGraph::KeyPress(int in_key) { 
	// Nothing to do for now
	return 1;
}

void Kis_IntGraph::DrawComponent() { 
	if (visible == 0)
		return;

	char backing[32];

	parent_panel->InitColorPref("panel_text_color", "white,black");
	parent_panel->ColorFromPref(color_fw, "panel_text_color");

	for (unsigned int x = 0; x < data_vec.size(); x++) {
		parent_panel->InitColorPref(data_vec[x].colorpref, data_vec[x].colordefault);
		parent_panel->ColorFromPref(data_vec[x].colorval, data_vec[x].colorpref);
	}

	// We want the same scale for over/under, so we'll calculate the
	// height as h - 1 (label) (div 2 if o/u)
	int gh = (ly - 1) / (graph_mode == 1 ? 2 : 1) - xgraph_size;
	// Zero position on the graph is the bottom, or center, depending
	// on normal or over/under
	int gzero = ey - (graph_mode == 1 ? gh : 1) - xgraph_size;
	// Width - label 
	int gw = lx;
	unsigned int gxofft;

	// Set the drawing max and min
	int dmax_y = max_y;
	int dmin_y = min_y;

	// Go through the list and get the max if we're auto-scaling
	if (max_y == 0 || min_y == 0) {
		for (unsigned int x = 0; x < data_vec.size(); x++) {
			for (unsigned int z = 0; z < data_vec[x].data->size(); z++) {
				if (max_y == 0 && 
					(((*(data_vec[x].data))[z] > 0 && 
					 dmax_y < (*(data_vec[x].data))[z]) ||
					((*(data_vec[x].data))[z] < 0 && 
					 dmax_y > (*(data_vec[x].data))[z])))
					dmax_y = (*(data_vec[x].data))[z];
			}
		}
	}

	// adjust the drawing size
	snprintf(backing, 32, " %d ", dmax_y);
	gxofft = strlen(backing);
	snprintf(backing, 32, " %d ", dmin_y);
	if (strlen(backing) > gxofft)
		gxofft = strlen(backing);
	gw -= gxofft;

	// Go through from least to greatest priority so that the "high" priority
	// draws over the old
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		int xmod = 0;
		int xgroup = 1;
		int dvsize = data_vec[x].data->size();

		if (inter_x) {
			xmod = ceilf((float) dvsize / (float) gw);
			xgroup = xmod * 2;
		}

		for (int gx = 0; gx < gw && inter_x; gx++) {
			int r = 0, py, nuse = 0;
			// We make the assumption here that T is a numerical
			// type in some fashion, if this is ever not true we'll have
			// to do something else
			// int avg = 0;
			int max = 0;

			// Interpolate down if we have too much data
			if (gw < dvsize) {
				// R is the range of samples we tied together to 
				// interpolate the graph to fit
				r = ((float) gx / (float) gw) * (float) dvsize;

				// Determine the local max across our range
				for (int pos = -1 * (xgroup / 2); pos < (xgroup / 2); pos++) {
					if (r + pos >= dvsize || r + pos < 0)
						continue;

					// Max depending on if we're neg or pos data
					if ((*(data_vec[x].data))[r + pos] >= 0 &&
						 (*(data_vec[x].data))[r + pos] > max) {
						if ((*(data_vec[x].data))[r+pos] > dmax_y) {
							max = dmax_y;
						} else {
							max = (*(data_vec[x].data))[r + pos];
						}
					} else if ((*(data_vec[x].data))[r + pos] <= 0 &&
						 (*(data_vec[x].data))[r + pos] < max) {
						if ((*(data_vec[x].data))[r+pos] < dmin_y) {
							max = dmin_y;
						} else {
							max = (*(data_vec[x].data))[r + pos];
						}
					}

					nuse++;
				} 
			} else {
				nuse = 1;
				// int gofft = kismin((1.0f / dvsize) * gw, gx);
				unsigned int pos = (unsigned int) (((float) gx/gw) * dvsize);
				if (pos >= (*(data_vec)[x].data).size() || pos < 0) {
					max = min_y;
				} else {
					max = (*(data_vec)[x].data)[pos];
				}
			}

			if (nuse == 0)
				continue;

			// If we're negative, do the math differently
			// Adapt the group max to our scale
			float adapted = 0;

			if (max < 0) {
				adapted = 
					(float) (abs(max) + dmin_y) /
					(float) (abs(dmax_y) + dmin_y);
			} else {
				adapted = (float) (max - min_y) / (float) (dmax_y - min_y);
			}

			// Scale it to the height of the graph
			py = (float) gh * adapted;

			// Set the color once
			wattrset(window, data_vec[x].colorval);

			// If we're plotting over/normal, we do nothing
			// If we're plotting under, we invert and draw below
			int oumod = 1;
			if (data_vec[x].overunder < 0 && graph_mode == 1)
				oumod = -1;

			for (int gy = gh; gy >= 0; gy--) {
				if (gy == py)
					mvwaddstr(window, gzero - (gy * oumod), sx + gx + gxofft, 
							  data_vec[x].line);
				else if (gy < py)
					mvwaddstr(window, gzero - (gy * oumod), sx + gx + gxofft, 
							  data_vec[x].fill);
			}
		}

		int rwidth = kismin(2, (1.0f / dvsize) * gw);
		for (int dvx = 0; dvx < dvsize && inter_x == 0; dvx++) {
			int py = 0;
			int max = (*(data_vec)[x].data)[dvx];
			int drawx = ((float) dvx / dvsize) * gw;

			// If we're negative, do the math differently
			// Adapt the group max to our scale
			float adapted = 0;

			if (max < 0) {
				adapted = 
					(float) (abs(max) + dmin_y) /
					(float) (abs(dmax_y) + dmin_y);
			} else {
				adapted = (float) (max - min_y) / (float) (dmax_y - min_y);
			}

			// Scale it to the height of the graph
			py = (float) gh * adapted;

			// Set the color once
			wattrset(window, data_vec[x].colorval);

			for (int rdx = rwidth * -1; rdx < rwidth; rdx++) {
				// If we're plotting over/normal, we do nothing
				// If we're plotting under, we invert and draw below
				int oumod = 1;
				if (data_vec[x].overunder < 0 && graph_mode == 1)
					oumod = -1;

				for (int gy = gh; gy >= 0; gy--) {
					if (gy == py)
						mvwaddstr(window, gzero - (gy * oumod), 
								  sx + drawx + rdx + gxofft, 
								  data_vec[x].line);
					else if (gy < py && data_vec[x].fill)
						mvwaddstr(window, gzero - (gy * oumod), 
								  sx + drawx + rdx + gxofft, 
								  data_vec[x].fill);
				}

			}
		}
	}

	// Draw the labels (right-hand)
	int posmod = 0, negmod = 0;
	// Set the backing blank
	memset(backing, ' ', 32);
	if ((maxlabel + 4) >=  32)
		maxlabel = (32 - 4);
	backing[maxlabel + 4] = '\0';
	// Draw the component name labels
	for (unsigned int x = 0; x < data_vec.size(); x++) {
		// Position
		int lpos = 0;
		// Text color
		if (data_vec[x].overunder < 0 && graph_mode == 1) {
			lpos = ey - negmod++;
		} else {
			lpos = sy + posmod++;
		}
		// Fill in the blocking
		wattrset(window, color_fw);
		mvwaddstr(window, lpos, ex - (maxlabel + 4), backing);
		// Fill in the label
		mvwaddstr(window, lpos, ex - (maxlabel), data_vec[x].name.c_str());

		// Fill in the colors
		wattrset(window, data_vec[x].colorval);
		mvwaddstr(window, lpos, ex - (maxlabel + 3), data_vec[x].line);
		mvwaddstr(window, lpos, ex - (maxlabel + 2), data_vec[x].fill);
	}

	// Draw the X marker labels
	wattrset(window, color_fw);
	for (unsigned int x = 0; x < label_x.size() && label_x_graphref >= 0; x++) {
		// GX within the # of samples on the graph
		int lgx = ((float) gw / data_vec[label_x_graphref].data->size()) * 
			label_x[x].position;
		for (unsigned int y = 0; y < label_x[x].label.size(); y++) {
			mvwaddch(window, gzero + y + 1, sx + lgx + gxofft, label_x[x].label[y]);
		}
	}

	// Reuse the backing for the scale
	if (draw_scale) {
		snprintf(backing, 32, " %d ", dmax_y);
		wattrset(window, color_fw);
		mvwaddstr(window, sy, sx, backing);

		wattrset(window, color_fw);
		mvwhline(window, gzero, sx, ACS_HLINE, lx);

		snprintf(backing, 32, " %d ", min_y);
		mvwaddstr(window, gzero, sx, backing);
	}
}

Kis_Panel::Kis_Panel(GlobalRegistry *in_globalreg, KisPanelInterface *in_intf) {
	globalreg = in_globalreg;
	kpinterface = in_intf;
	win = newwin(0, 0, 0, 0);
	pan = new_panel(win);
	menu = NULL;

	text_color = border_color = 0;
	InitColorPref("panel_text_color", "white,black");
	InitColorPref("panel_border_color", "blue,black");
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	sx = sy = sizex = sizey = 0;

	active_component = NULL;
	tab_pos = -1;
}

Kis_Panel::~Kis_Panel() {
	for (unsigned int x = 0; x < pan_comp_vec.size(); x++) {
		delete pan_comp_vec[x].comp;
	}

	if (pan != NULL)
		del_panel(pan);
	if (win != NULL)
		delwin(win);
}

void Kis_Panel::AddComponentVec(Kis_Panel_Component *in_comp, int in_flags) {
	component_entry etr;

	etr.comp_flags = in_flags;
	etr.comp = in_comp;

	pan_comp_vec.push_back(etr);
}

void Kis_Panel::DelComponentVec(Kis_Panel_Component *in_comp) {
	for (unsigned int x = 0; x < pan_comp_vec.size(); x++) {
		if (pan_comp_vec[x].comp == in_comp) {
			pan_comp_vec.erase(pan_comp_vec.begin() + x);
			return;
		}
	}
}

int Kis_Panel::KeyPress(int in_key) {
	int ret;

	if (menu) {
		ret = menu->KeyPress(in_key);

		if (ret != 0)
			return 0;
	}

	// figure out if we need to get to a visible item first and jump to it via the
	// tab function
	if (active_component != NULL && active_component->GetVisible() == 0 &&
		in_key != '\t')
		KeyPress('\t');

	if (in_key == '\t' && tab_pos >= 0) {
		int set = -1;

		// Find from current to end
		for (unsigned int x = tab_pos + 1; x < pan_comp_vec.size(); x++) {
			if ((pan_comp_vec[x].comp_flags & KIS_PANEL_COMP_TAB) == 0 ||
				(pan_comp_vec[x].comp->GetVisible() == 0))
				continue;

			set = x;
			break;
		}

		// No?  Find from start
		if (set == -1) {
			for (unsigned int x = 0; x < pan_comp_vec.size(); x++) {
				if ((pan_comp_vec[x].comp_flags & KIS_PANEL_COMP_TAB) == 0 ||
					(pan_comp_vec[x].comp->GetVisible() == 0))
					continue;

				set = x;
				break;
			}
		}

		// No?  Someone deleted the tabable components then, just stop
		if (set == -1) {
			tab_pos = -1;
			return 0;
		}

		pan_comp_vec[tab_pos].comp->Deactivate();
		tab_pos = set;

		pan_comp_vec[tab_pos].comp->Activate(1);
		active_component = pan_comp_vec[tab_pos].comp;
	}

	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);
		return 0;
	}

	return 0;
}

int Kis_Panel::MouseEvent(MEVENT *mevent) {
	int ret;

	// We just process every component until we get a non-0
	for (unsigned int x = 0; x < pan_comp_vec.size(); x++) {
		if ((pan_comp_vec[x].comp_flags & KIS_PANEL_COMP_EVT) == 0)
			continue;

		ret = pan_comp_vec[x].comp->MouseEvent(mevent);

		if (ret != 0) {
			// Positive response means switch the focus
			if (ret >= 0) {
				if (active_component != NULL) 
					active_component->Deactivate();

				if ((pan_comp_vec[x].comp_flags & KIS_PANEL_COMP_TAB) &&
					tab_pos >= 0) {
					tab_pos = x;
				}

				active_component = pan_comp_vec[x].comp;
				active_component->Activate(0);
			}

			return 0;
		}
	}

	return 0;
}

void Kis_Panel::InitColorPref(string in_pref, string in_def) {
	if (kpinterface->prefs.FetchOpt(in_pref) == "")
		kpinterface->prefs.SetOpt(in_pref, in_def, 1);
}

void Kis_Panel::ColorFromPref(int &clr, string in_pref) {
	if (kpinterface->prefs.FetchOptDirty(in_pref) || clr == 0) {
		kpinterface->prefs.SetOptDirty(in_pref, 0);
		clr = kpinterface->colors.AddColor(kpinterface->prefs.FetchOpt(in_pref));
	}

	return;
}

int Kis_Panel::AddColor(string in_color) {
	return kpinterface->colors.AddColor(in_color);
}

void Kis_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	sx = in_sx;
	sy = in_sy;
	sizex = in_x;
	sizey = in_y;

	if (win == NULL) {
		win = newwin(sizey, sizex, sy, sx);
	}

	if (pan == NULL) {
		pan = new_panel(win);
	} else {
		wresize(win, sizey, sizex);
		replace_panel(pan, win);
		move_panel(pan, sy, sx);
	}

	keypad(win, true);
}

int Kis_Panel::Poll() {
	int get = wgetch(win);
	MEVENT mevent;
	int ret;

	if (get == KEY_MOUSE) {
		getmouse(&mevent);
		ret = MouseEvent(&mevent);
	} else {
		ret = KeyPress(get);
	}

	if (ret < 0)
		return ret;

	return 1;
}

void Kis_Panel::SetTitle(string in_title) {
	title = in_title;
}

void Kis_Panel::DrawTitleBorder() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wattrset(win, border_color);
	box(win, 0, 0);
	wattron(win, WA_UNDERLINE);
	mvwaddstr(win, 0, 3, title.c_str());
	wattroff(win, WA_UNDERLINE);

	wattrset(win, text_color);
}

void Kis_Panel::DrawComponentVec() {
	wattrset(win, text_color);
	for (unsigned int x = 0; x < pan_comp_vec.size(); x++) {
		if ((pan_comp_vec[x].comp_flags & KIS_PANEL_COMP_DRAW) == 0)
			continue;

		pan_comp_vec[x].comp->DrawComponent();
	}

	if (menu != NULL)
		menu->DrawComponent();
}

#endif

