/*
 * imgui_md: Markdown for Dear ImGui using MD4C
 * (http://https://github.com/mekhontsev/imgui_md)
 *
 * Copyright (c) 2021 Dmitry Mekhontsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "imgui_md.h"

#include <cstdlib>  // MCDF: std::atof for image width= hint
#include <string>

imgui_md::imgui_md()
{
	m_md.abi_version = 0;

	m_md.flags = MD_FLAG_TABLES | MD_FLAG_UNDERLINE | MD_FLAG_STRIKETHROUGH;

	m_md.enter_block = [](MD_BLOCKTYPE t, void* d, void* u) {
		return ((imgui_md*)u)->block(t, d, true);
	};

	m_md.leave_block = [](MD_BLOCKTYPE t, void* d, void* u) {
		return ((imgui_md*)u)->block(t, d, false);
	};

	m_md.enter_span = [](MD_SPANTYPE t, void* d, void* u) {
		return ((imgui_md*)u)->span(t, d, true);
	};

	m_md.leave_span = [](MD_SPANTYPE t, void* d, void* u) {
		return ((imgui_md*)u)->span(t, d, false);
	};

	m_md.text = [](MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE size, void* u) {
		return ((imgui_md*)u)->text(t, text, text + size);
	};

	m_md.debug_log = nullptr;

	m_md.syntax = nullptr;

	////////////////////////////////////////////////////////////////////////////

	m_table_last_pos = ImVec2(0, 0);

}



void imgui_md::BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e)
{
	if (e) {
		m_list_stack.push_back(list_info{ 0, d->mark, false });
	} else {
		m_list_stack.pop_back();
		if (m_list_stack.empty())ImGui::NewLine();
	}
}

void imgui_md::BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e)
{
	if (e) {
		m_list_stack.push_back(list_info{ d->start, d->mark_delimiter, true });
	} else {
		m_list_stack.pop_back();
		if (m_list_stack.empty())ImGui::NewLine();
	}
}

void imgui_md::BLOCK_LI(const MD_BLOCK_LI_DETAIL*, bool e)
{
	if (e) {
		ImGui::NewLine();

		list_info& nfo = m_list_stack.back();
		if (nfo.is_ol) {
			ImGui::Text("%d%c", nfo.cur_ol++, nfo.delim);
			ImGui::SameLine();
		} else {
			if (nfo.delim == '*') {
				float cx = ImGui::GetCursorPosX();
				cx -= ImGui::GetStyle().FramePadding.x * 2;
				ImGui::SetCursorPosX(cx);
				ImGui::Bullet();
			} else {
				ImGui::Text("%c", nfo.delim);
				ImGui::SameLine();
			}
		}

		ImGui::Indent();
	} else {
		ImGui::Unindent();
	}
}

void imgui_md::BLOCK_HR(bool e)
{
	if (!e) {
		ImGui::NewLine();
		ImGui::Separator();

	}
}

void imgui_md::BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e)
{
	if (e) {
		m_hlevel = d->level;
		ImGui::NewLine();
	} else {
		m_hlevel = 0;
	}

	set_font(e);

	if (!e) {
		if (d->level <= 2) {
			ImGui::NewLine();
			ImGui::Separator();
		}
	}
}

void imgui_md::BLOCK_DOC(bool e)
{
	if (e) m_code_id = 0;  // reset the per-render code-block child id counter
}

void imgui_md::BLOCK_QUOTE(bool)
{

}
void imgui_md::BLOCK_CODE(const MD_BLOCK_CODE_DETAIL*, bool e)
{
	// MCDF: render fenced code blocks in the monospace face inside a framed,
	// auto-height box (get_font() returns the mono font while m_is_code is set).
	m_is_code = e;
	if (e) {
		ImGui::NewLine();
		ImGui::PushID(static_cast<int>(m_code_id++));
		ImGui::BeginChild("##mcdf_code", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
		set_font(true);
	} else {
		set_font(false);
		ImGui::EndChild();
		ImGui::PopID();
		ImGui::NewLine();
	}
}

void imgui_md::BLOCK_HTML(bool)
{

}

void imgui_md::BLOCK_P(bool)
{
	if (!m_list_stack.empty())return;
	ImGui::NewLine();
}

void imgui_md::BLOCK_TABLE(const MD_BLOCK_TABLE_DETAIL* d, bool e)
{
	// MCDF: render Markdown tables through Dear ImGui's real table API so columns
	// auto-size with borders + row striping, instead of the original cursor-
	// position layout (which collapsed into narrow, letter-wrapped columns).
	if (e) {
		int cols = (d && d->col_count > 0) ? static_cast<int>(d->col_count) : 1;
		const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_NoHostExtendX;
		m_in_table = ImGui::BeginTable("##imgui_md_table", cols, flags);
	} else if (m_in_table) {
		ImGui::EndTable();
		m_in_table = false;
	}
}

void imgui_md::BLOCK_THEAD(bool e)
{
	m_is_table_header = e;
	if (m_table_header_highlight)set_font(e);
}

void imgui_md::BLOCK_TBODY(bool e)
{
	m_is_table_body = e;
}

void imgui_md::BLOCK_TR(bool e)
{
	if (e && m_in_table) ImGui::TableNextRow();
}

void imgui_md::BLOCK_TH(const MD_BLOCK_TD_DETAIL* d, bool e)
{
	BLOCK_TD(d, e);

}

void imgui_md::BLOCK_TD(const MD_BLOCK_TD_DETAIL*, bool e)
{
	if (e && m_in_table) ImGui::TableNextColumn();
}

////////////////////////////////////////////////////////////////////////////////
void imgui_md::set_href(bool e, const MD_ATTRIBUTE& src)
{
	if (e) {
		m_href.assign(src.text, src.size);
	} else {
		m_href.clear();
	}
}

void imgui_md::set_font(bool e)
{
	if (e) {
		// MCDF: scale heading text (H1..H6) larger than body per Markdown
		// convention. imgui 1.92 lets PushFont set a pixel size (0 = inherit).
		float px = 0.0f;
		if (m_hlevel > 0) {
			static const float kScale[] = {1.0f, 1.9f, 1.55f, 1.32f, 1.15f, 1.05f, 1.0f};
			const int lvl = (m_hlevel <= 6) ? static_cast<int>(m_hlevel) : 6;
			const ImGuiStyle& st = ImGui::GetStyle();
			px = st.FontSizeBase * st.FontScaleMain * kScale[lvl];
		}
		ImGui::PushFont(get_font(), px);
	} else {
		ImGui::PopFont();
	}
}

void imgui_md::set_color(bool e)
{
	if (e) {
		ImGui::PushStyleColor(ImGuiCol_Text, get_color());
	} else {
		ImGui::PopStyleColor();
	}
}

void imgui_md::line(ImColor c, bool under)
{
	ImVec2 mi = ImGui::GetItemRectMin();
	ImVec2 ma = ImGui::GetItemRectMax();

	if (!under) {
		ma.y -= ImGui::GetFontSize() / 2;
	}

	mi.y = ma.y;

	ImGui::GetWindowDrawList()->AddLine(mi, ma, c, 1.0f);
}

void imgui_md::SPAN_A(const MD_SPAN_A_DETAIL* d, bool e)
{
	set_href(e, d->href);
	set_color(e);
}


void imgui_md::SPAN_EM(bool e)
{
	m_is_em = e;
	set_font(e);
}

void imgui_md::SPAN_STRONG(bool e)
{
	m_is_strong = e;
	set_font(e);
}


void imgui_md::SPAN_IMG(const MD_SPAN_IMG_DETAIL* d, bool e)
{
	m_is_image = e;

	set_href(e, d->src);

	if (e) {

		image_info nfo;
		if (get_image(nfo)) {

			const float scale = ImGui::GetIO().FontGlobalScale;
			nfo.size.x *= scale;
			nfo.size.y *= scale;

			// MCDF: honor "width=<px>" and "align=left|center|right" hints in the
			// image title, e.g. ![alt](assets/x.png "width=400 align=center").
			int align = 0;  // 0 left, 1 center, 2 right
			if (d->title.size && nfo.size.x > 0.0f) {
				const std::string tt(d->title.text, d->title.size);
				const float ratio = nfo.size.y / nfo.size.x;
				const auto wp = tt.find("width=");
				if (wp != std::string::npos) {
					const float w = (float)std::atof(tt.c_str() + wp + 6);
					if (w > 0.0f) { nfo.size.x = w; nfo.size.y = w * ratio; }
				}
				if (tt.find("align=center") != std::string::npos) align = 1;
				else if (tt.find("align=right") != std::string::npos) align = 2;
			}

			ImVec2 const csz = ImGui::GetContentRegionAvail();
			if (nfo.size.x > csz.x) {
				const float r = nfo.size.y / nfo.size.x;
				nfo.size.x = csz.x;
				nfo.size.y = csz.x * r;
			}
			if (align == 1) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (csz.x - nfo.size.x) * 0.5f);
			else if (align == 2) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (csz.x - nfo.size.x));

			ImGui::Image(nfo.texture_id, nfo.size, nfo.uv0, nfo.uv1, nfo.col_tint, nfo.col_border);

			if (ImGui::IsItemHovered()) {

				//if (d->title.size) {
				//	ImGui::SetTooltip("%.*s", (int)d->title.size, d->title.text);
				//}

				if (ImGui::IsMouseReleased(0)) {
					open_url();
				}
			}
		}
	}
}

void imgui_md::SPAN_CODE(bool e)
{
	// MCDF: inline `code` in the monospace face.
	m_is_code = e;
	set_font(e);
}


void imgui_md::SPAN_LATEXMATH(bool)
{

}

void imgui_md::SPAN_LATEXMATH_DISPLAY(bool)
{

}

void imgui_md::SPAN_WIKILINK(const MD_SPAN_WIKILINK_DETAIL*, bool)
{

}

void imgui_md::SPAN_U(bool e)
{
	m_is_underline = e;
}

void imgui_md::SPAN_DEL(bool e)
{
	m_is_strikethrough = e;
}

void imgui_md::render_text(const char* str, const char* str_end)
{
	const float scale = ImGui::GetIO().FontGlobalScale;
	const ImGuiStyle& s = ImGui::GetStyle();
	bool is_lf = false;

	// MCDF: ImGui trims a text item's leading/trailing spaces, which drops the
	// separator around inline links ("and [link] are" -> "and[link]are"). Trim
	// the edge spaces here and represent them with explicit SameLine spacing so
	// they survive regardless; interior word spaces render normally. (The space
	// width is measured from an interior space so it is not itself trimmed.)
	// Code text keeps all its whitespace (indentation is significant, and code
	// has no inline links), so only prose gets the edge-space handling.
	const bool code = m_is_code;
	const float sp_w = ImGui::CalcTextSize("x x").x - ImGui::CalcTextSize("xx").x;
	const bool lead_space = !code && (str < str_end && *str == ' ');
	const bool trail_space = !code && (str_end > str && *(str_end - 1) == ' ');
	if (!code) {
		while (str < str_end && *str == ' ') ++str;
		while (str_end > str && *(str_end - 1) == ' ') --str_end;
	}
	if (str >= str_end) {  // the run was only whitespace
		ImGui::SameLine(0.0f, (lead_space || trail_space) ? sp_w : 0.0f);
		return;
	}
	if (lead_space) ImGui::SameLine(0.0f, sp_w);

	while (!m_is_image && str < str_end) {

		const char* te = str_end;

		{
			// MCDF: wrap at the current content region - the window, or a table
			// cell's width when inside a Dear ImGui table.
			const float wl = ImGui::GetContentRegionAvail().x;
			te = ImGui::GetFont()->CalcWordWrapPositionA(scale, str, str_end, wl);
			if (te == str)++te;
		}

		
		ImGui::TextUnformatted(str, te);

		if (te > str && *(te - 1) == '\n') {
			is_lf = true;
		}
		
		if (!m_href.empty()) {

			ImVec4 c;
			if (ImGui::IsItemHovered()) {

				ImGui::SetTooltip("%s", m_href.c_str());

				c = s.Colors[ImGuiCol_ButtonHovered];
				if (ImGui::IsMouseReleased(0)) {
					open_url();
				}
			} else {
				c = s.Colors[ImGuiCol_Button];
			}
			line(c, true);
		}
		if (m_is_underline) {
			line(s.Colors[ImGuiCol_Text], true);
		}
		if (m_is_strikethrough) {
			line(s.Colors[ImGuiCol_Text], false);
		}

		str = te;

		while (str < str_end && *str == ' ')++str;  // skip spaces at a wrap boundary
	}

	if (!is_lf)ImGui::SameLine(0.0f, trail_space ? sp_w : 0.0f);
}


bool imgui_md::render_entity(const char* str, const char* str_end)
{
	const size_t sz = str_end - str;
	if (strncmp(str, "&nbsp;", sz) == 0) {
		ImGui::TextUnformatted(""); ImGui::SameLine();
		return true;
	}
	return false;
}

static bool skip_spaces(const std::string& d, size_t& p)
{
	for (; p < d.length(); ++p) {
		if (d[p] != ' ' && d[p] != '\t') {
			break;
		}
	}
	return p < d.length();
}

static std::string get_div_class(const char* str, const char* str_end)
{
	if (str_end <= str)return "";

	std::string d(str, str_end - str);
	if (d.back() == '>')d.pop_back();

	const char attr[] = "class";
	size_t p = d.find(attr);
	if (p == std::string::npos)return "";
	p += sizeof(attr)-1;

	if (!skip_spaces(d, p))return "";

	if (d[p] != '=')return "";
	++p;

	if (!skip_spaces(d, p))return "";

	bool has_q = false;

	if (d[p] == '"' || d[p] == '\'') {
		has_q = true;
		++p;
	}
	if (p == d.length())return "";

	if (!has_q) {
		if (!skip_spaces(d, p))return "";
	}

	size_t pe;
	for (pe = p; pe < d.length(); ++pe) {

		const char c = d[pe];

		if (has_q) {
			if (c == '"' || c == '\'') {
				break;
			}
		} else {
			if (c == ' ' || c == '\t') {
				break;
			}
		}
	}

	return d.substr(p, pe - p);

}

bool imgui_md::check_html(const char* str, const char* str_end)
{
	const size_t sz = str_end - str;

	if (strncmp(str, "<br>", sz) == 0) {
		ImGui::NewLine();
		return true;
	}
	if (strncmp(str, "<hr>", sz) == 0) {
		ImGui::Separator();
		return true;
	}
	if (strncmp(str, "<u>", sz) == 0) {
		m_is_underline = true;
		return true;
	}
	if (strncmp(str, "</u>", sz) == 0) {
		m_is_underline = false;
		return true;
	}

	const size_t div_sz = 4;
	if (strncmp(str, "<div", sz > div_sz ? div_sz : sz) == 0) {
		m_div_stack.emplace_back(get_div_class(str + div_sz, str_end));
		html_div(m_div_stack.back(), true);
		return true;
	}
	if (strncmp(str, "</div>", sz) == 0) {
		if (m_div_stack.empty())return false;
		html_div(m_div_stack.back(), false);
		m_div_stack.pop_back();
		return true;
	}
	return false;
}


void imgui_md::html_div(const std::string& dclass, bool e)
{
	//Example:
#if 0 
	if (dclass == "red") {
		if (e) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		} else {
			ImGui::PopStyleColor();
		}
	}
#endif
	dclass; e;
}


int imgui_md::text(MD_TEXTTYPE type, const char* str, const char* str_end)
{
	switch (type) {
	case MD_TEXT_NORMAL:
		render_text(str, str_end);
		break;
	case MD_TEXT_CODE:
		render_text(str, str_end);
		break;
	case MD_TEXT_NULLCHAR:
		break;
	case MD_TEXT_BR:
		ImGui::NewLine();
		break;
	case MD_TEXT_SOFTBR:
		soft_break();
		break;
	case MD_TEXT_ENTITY:
		if (!render_entity(str, str_end)) {
			render_text(str, str_end);
		};
		break;
	case MD_TEXT_HTML:
		if (!check_html(str, str_end)) {
			render_text(str, str_end);
		}
		break;
	case MD_TEXT_LATEXMATH:
		render_text(str, str_end);
		break;
	default:
		break;
	}

	if (m_is_table_header) {
		const float x = ImGui::GetCursorPosX();
		if (x > m_table_last_pos.x)m_table_last_pos.x = x;
	}

	return 0;
}

int imgui_md::block(MD_BLOCKTYPE type, void* d, bool e)
{
	switch (type)
	{
	case MD_BLOCK_DOC:
		BLOCK_DOC(e);
		break;
	case MD_BLOCK_QUOTE:
		BLOCK_QUOTE(e);
		break;
	case MD_BLOCK_UL:
		BLOCK_UL((MD_BLOCK_UL_DETAIL*)d, e);
		break;
	case MD_BLOCK_OL:
		BLOCK_OL((MD_BLOCK_OL_DETAIL*)d, e);
		break;
	case MD_BLOCK_LI:
		BLOCK_LI((MD_BLOCK_LI_DETAIL*)d, e);
		break;
	case MD_BLOCK_HR:
		BLOCK_HR(e);
		break;
	case MD_BLOCK_H:
		BLOCK_H((MD_BLOCK_H_DETAIL*)d, e);
		break;
	case MD_BLOCK_CODE:
		BLOCK_CODE((MD_BLOCK_CODE_DETAIL*)d, e);
		break;
	case MD_BLOCK_HTML:
		BLOCK_HTML(e);
		break;
	case MD_BLOCK_P:
		BLOCK_P(e);
		break;
	case MD_BLOCK_TABLE:
		BLOCK_TABLE((MD_BLOCK_TABLE_DETAIL*)d, e);
		break;
	case MD_BLOCK_THEAD:
		BLOCK_THEAD(e);
		break;
	case MD_BLOCK_TBODY:
		BLOCK_TBODY(e);
		break;
	case MD_BLOCK_TR:
		BLOCK_TR(e);
		break;
	case MD_BLOCK_TH:
		BLOCK_TH((MD_BLOCK_TD_DETAIL*)d, e);
		break;
	case MD_BLOCK_TD:
		BLOCK_TD((MD_BLOCK_TD_DETAIL*)d, e);
		break;
	default:
		assert(false);
		break;
	}

	return 0;
}

int imgui_md::span(MD_SPANTYPE type, void* d, bool e)
{
	switch (type)
	{
	case MD_SPAN_EM:
		SPAN_EM(e);
		break;
	case MD_SPAN_STRONG:
		SPAN_STRONG(e);
		break;
	case MD_SPAN_A:
		SPAN_A((MD_SPAN_A_DETAIL*)d, e);
		break;
	case MD_SPAN_IMG:
		SPAN_IMG((MD_SPAN_IMG_DETAIL*)d, e);
		break;
	case MD_SPAN_CODE:
		SPAN_CODE(e);
		break;
	case MD_SPAN_DEL:
		SPAN_DEL(e);
		break;
	case MD_SPAN_LATEXMATH:
		SPAN_LATEXMATH(e);
		break;
	case MD_SPAN_LATEXMATH_DISPLAY:
		SPAN_LATEXMATH_DISPLAY(e);
		break;
	case MD_SPAN_WIKILINK:
		SPAN_WIKILINK((MD_SPAN_WIKILINK_DETAIL*)d, e);
		break;
	case MD_SPAN_U:
		SPAN_U(e);
		break;
	default:
		assert(false);
		break;
	}

	return 0;
}

int imgui_md::print(const char* str, const char* str_end)
{
	if (str >= str_end)return 0;
	return md_parse(str, (MD_SIZE)(str_end - str), &m_md, this);
}

////////////////////////////////////////////////////////////////////////////////

ImFont* imgui_md::get_font() const
{
	return nullptr;//default font

	//Example:
#if 0
	if (m_is_table_header) {
		return g_font_bold;
	}

	switch (m_hlevel)
	{
	case 0:
		return m_is_strong ? g_font_bold : g_font_regular;
	case 1:
		return g_font_bold_large;
	default:
		return g_font_bold;
	}
#endif

};

ImVec4 imgui_md::get_color() const
{
	if (!m_href.empty()) {
		return ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
	}
	return  ImGui::GetStyle().Colors[ImGuiCol_Text];
}


bool imgui_md::get_image(image_info& nfo) const
{
	//Use m_href to identify images
	
	//Example - Imgui font texture
	// MCDF vendor patch: Dear ImGui 1.92 (IMGUI_VERSION_NUM >= 19200) made
	// Fonts->TexID an ImTextureRef; image_info::texture_id is ImTextureID, so
	// extract it explicitly. Older ImGui assigns directly. Guarded so the build
	// works whichever imgui version vcpkg resolves on each platform.
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
	nfo.texture_id = ImGui::GetIO().Fonts->TexID.GetTexID();
#else
	nfo.texture_id = ImGui::GetIO().Fonts->TexID;
#endif
	nfo.size = { 100,50 };
	nfo.uv0 = { 0,0 };
	nfo.uv1 = { 1,1 };
	nfo.col_tint = { 1,1,1,1 };
	nfo.col_border = { 0,0,0,0 };

	return true;
};

void imgui_md::open_url() const
{
	//Example:

#if 0	
	if (!m_is_image) {
		SDL_OpenURL(m_href.c_str());
	} else {
		//image clicked
	}
#endif
}

void imgui_md::soft_break()
{
	// MCDF: a soft line break inside a paragraph (a source-wrapped line, e.g.
	// CRLF/LF between "vcpkg" and "are") is whitespace - render it as a space so
	// the lines keep their separator and reflow at the window width, instead of
	// joining tightly ("vcpkg\nare" -> "vcpkgare", "declared in\n[link]" ->
	// "declared in[link]"). Measured from an interior space so it isn't trimmed.
	const float sp_w = ImGui::CalcTextSize("x x").x - ImGui::CalcTextSize("xx").x;
	ImGui::SameLine(0.0f, sp_w);
}
