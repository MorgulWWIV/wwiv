/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*             Copyright (C)1998-2020, WWIV Software Services             */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/*                                                                        */
/**************************************************************************/
#include "bbs/readmail.h"

#include "bbs/bbs.h"
#include "bbs/bbsovl1.h"
#include "bbs/bbsutl.h"
#include "bbs/bbsutl1.h"
#include "bbs/bbsutl2.h"
#include "bbs/com.h"
#include "bbs/conf.h"
#include "bbs/connect1.h"
#include "bbs/datetime.h"
#include "bbs/email.h"
#include "bbs/execexternal.h"
#include "bbs/extract.h"
#include "bbs/input.h"
#include "bbs/instmsg.h"
#include "bbs/message_file.h"
#include "bbs/mmkey.h"
#include "bbs/msgbase1.h"
#include "bbs/pause.h"
#include "bbs/printfile.h"
#include "bbs/quote.h"
#include "bbs/read_message.h"
#include "bbs/shortmsg.h"
#include "bbs/showfiles.h"
#include "bbs/sr.h"
#include "bbs/subacc.h"
#include "bbs/sublist.h"
#include "bbs/sysopf.h"
#include "bbs/sysoplog.h"
#include "bbs/utility.h"
#include "bbs/workspace.h"
#include "bbs/xfer.h"
#include "core/stl.h"
#include "core/strings.h"
#include "core/textfile.h"
#include "fmt/printf.h"
#include "local_io/wconstants.h"
#include "sdk/filenames.h"
#include "sdk/msgapi/message_utils_wwiv.h"
#include "sdk/names.h"
#include "sdk/status.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using std::string;
using std::unique_ptr;
using std::vector;
using namespace wwiv::core;
using namespace wwiv::sdk;
using namespace wwiv::sdk::msgapi;
using namespace wwiv::stl;
using namespace wwiv::strings;

// Implementation
static bool same_email(tmpmailrec& tm, const mailrec& m) {
  if (tm.fromsys != m.fromsys || tm.fromuser != m.fromuser || m.tosys != 0 ||
      m.touser != a()->usernum || tm.daten != m.daten || tm.index == -1 ||
      memcmp(&tm.msg, &m.msg, sizeof(messagerec)) != 0) {
    return false;
  }
  return true;
}

static void purgemail(vector<tmpmailrec>& mloc, int mw, int* curmail, mailrec* m1, slrec* sl) {
  mailrec m{};

  if (m1->anony & anony_sender && (sl->ability & ability_read_email_anony) == 0) {
    bout << "|#5Delete all mail to you from this user? ";
  } else {
    bout << "|#5Delete all mail to you from ";
    if (m1->fromsys) {
      bout << "#" << m1->fromuser << " @" << m1->fromsys << "? ";
    } else {
      if (m1->fromuser == 65535) {
        bout << "Networks? ";
      } else {
        bout << "#" << m1->fromuser << "? ";
      }
    }
  }
  if (yesno()) {
    auto pFileEmail(OpenEmailFile(true));
    if (!pFileEmail->IsOpen()) {
      return;
    }
    for (int i = 0; i < mw; i++) {
      if (mloc[i].index >= 0) {
        pFileEmail->Seek(mloc[i].index * sizeof(mailrec), File::Whence::begin);
        pFileEmail->Read(&m, sizeof(mailrec));
        if (same_email(mloc[i], m)) {
          if (m.fromuser == m1->fromuser && m.fromsys == m1->fromsys) {
            bout << "Deleting mail msg #" << i + 1 << wwiv::endl;
            delmail(*pFileEmail, mloc[i].index);
            mloc[i].index = -1;
            if (*curmail == i) {
              ++(*curmail);
            }
          }
        }
      } else {
        if (*curmail == i) {
          ++(*curmail);
        }
      }
    }
    pFileEmail->Close();
  }
}

static void resynch_email(vector<tmpmailrec>& mloc, int mw, int rec, mailrec* m, int del,
                          unsigned short stat) {
  int i;
  mailrec m1{};

  auto pFileEmail(OpenEmailFile(del || stat));
  if (pFileEmail->IsOpen()) {
    const auto mfl = static_cast<File::size_type>(pFileEmail->length() / sizeof(mailrec));

    for (i = 0; i < mw; i++) {
      if (mloc[i].index >= 0) {
        mloc[i].index = -2;
      }
    }

    int mp = 0;

    for (i = 0; i < mfl; i++) {
      pFileEmail->Seek(i * sizeof(mailrec), File::Whence::begin);
      pFileEmail->Read(&m1, sizeof(mailrec));

      if (m1.tosys == 0 && m1.touser == a()->usernum) {
        for (int i1 = mp; i1 < mw; i1++) {
          if (same_email(mloc[i1], m1)) {
            mloc[i1].index = static_cast<int16_t>(i);
            mp = i1 + 1;
            if (i1 == rec) {
              *m = m1;
            }
            break;
          }
        }
      }
    }

    for (i = 0; i < mw; i++) {
      if (mloc[i].index == -2) {
        mloc[i].index = -1;
      }
    }

    if (stat && !del && (mloc[rec].index >= 0)) {
      m->status |= stat;
      pFileEmail->Seek(mloc[rec].index * sizeof(mailrec), File::Whence::begin);
      pFileEmail->Write(m, sizeof(mailrec));
    }
    if (del && (mloc[rec].index >= 0)) {
      if (del == 2) {
        m->touser = 0;
        m->tosys = 0;
        m->daten = 0xffffffff;
        m->msg.storage_type = 0;
        m->msg.stored_as = 0xffffffff;
        pFileEmail->Seek(mloc[rec].index * sizeof(mailrec), File::Whence::begin);
        pFileEmail->Write(m, sizeof(mailrec));
      } else {
        delmail(*pFileEmail, mloc[rec].index);
      }
      mloc[rec].index = -1;
    }
    pFileEmail->Close();
  } else {
    mloc[rec].index = -1;
  }
}

// used in qwk1.cpp
bool read_same_email(std::vector<tmpmailrec>& mloc, int mw, int rec, mailrec& m, int del,
                     unsigned short stat) {
  if (mloc[rec].index < 0) {
    return false;
  }

  unique_ptr<File> pFileEmail(OpenEmailFile(del || stat));
  pFileEmail->Seek(mloc[rec].index * sizeof(mailrec), File::Whence::begin);
  pFileEmail->Read(&m, sizeof(mailrec));

  if (!same_email(mloc[rec], m)) {
    pFileEmail->Close();
    a()->status_manager()->RefreshStatusCache();
    if (a()->emchg_) {
      resynch_email(mloc, mw, rec, &m, del, stat);
    } else {
      mloc[rec].index = -1;
    }
  } else {
    if (stat && !del && (mloc[rec].index >= 0)) {
      m.status |= stat;
      pFileEmail->Seek(mloc[rec].index * sizeof(mailrec), File::Whence::begin);
      pFileEmail->Write(&m, sizeof(mailrec));
    }
    if (del) {
      if (del == 2) {
        m.touser = 0;
        m.tosys = 0;
        m.daten = 0xffffffff;
        m.msg.storage_type = 0;
        m.msg.stored_as = 0xffffffff;
        pFileEmail->Seek(mloc[rec].index * sizeof(mailrec), File::Whence::begin);
        pFileEmail->Write(&m, sizeof(mailrec));
      } else {
        delmail(*pFileEmail, mloc[rec].index);
      }
      mloc[rec].index = -1;
    }
    pFileEmail->Close();
  }
  return (mloc[rec].index != -1);
}

static void add_netsubscriber(const net_networks_rec& net, int network_number, int system_number) {
  if (!valid_system(system_number)) {
    system_number = 0;
  }

  bout.nl();
  bout << "|#1Adding subscriber to subscriber list...\r\n\n";
  bout << "|#2SubType: ";
  const auto subtype = input(7, true);
  if (subtype.empty()) {
    return;
  }
  const auto fn = StrCat(net.dir, "n", subtype, ".net");
  if (!File::Exists(fn)) {
    bout.nl();
    bout << "|#6Subscriber file not found: " << fn << wwiv::endl;
    return;
  }
  bout.nl();
  if (system_number) {
    bout << "Add @" << system_number << "." << net.name << " to subtype " << subtype << "? ";
  }
  if (!system_number || !noyes()) {
    bout << "|#2System Number: ";
    const auto s = input(5, true);
    if (s.empty()) {
      return;
    }
    system_number = to_number<int>(s);
    if (!valid_system(system_number)) {
      bout << "@" << system_number << " is not a valid system in " << net.name << ".\r\n\n";
      return;
    }
  }
  TextFile host_file(fn, "a+t");
  if (host_file.IsOpen()) {
    host_file.Write(fmt::sprintf("%u\n", system_number));
    host_file.Close();
    // TODO find replacement for autosend.exe
    if (File::Exists("autosend.exe")) {
      bout << "AutoSend starter messages? ";
      if (yesno()) {
        const auto autosend = FilePath(a()->bindir(), "autosend");
        const auto cmd = StrCat(autosend.string(), " ", subtype, " ", system_number, " .", network_number);
        ExecuteExternalProgram(cmd, EFLAG_NONE);
      }
    }
  }
}

void delete_attachment(unsigned long daten, int forceit) {
  filestatusrec fsr{};

  auto found = false;
  File fileAttach(FilePath(a()->config()->datadir(), ATTACH_DAT));
  if (fileAttach.Open(File::modeBinary | File::modeReadWrite)) {
    auto l = fileAttach.Read(&fsr, sizeof(fsr));
    while (l > 0 && !found) {
      if (daten == static_cast<unsigned long>(fsr.id)) {
        found = true;
        fsr.id = 0;
        fileAttach.Seek(static_cast<long>(sizeof(filestatusrec)) * -1L, File::Whence::current);
        fileAttach.Write(&fsr, sizeof(filestatusrec));
        auto delfile = true;
        if (!forceit) {
          if (so()) {
            bout << "|#5Delete attached file? ";
            delfile = yesno();
          }
        }
        if (delfile) {
          File::Remove(FilePath(a()->GetAttachmentDirectory(), fsr.filename));
        } else {
          bout << "\r\nOrphaned attach " << fsr.filename << " remains in "
               << a()->GetAttachmentDirectory() << wwiv::endl;
          pausescr();
        }
      } else {
        l = fileAttach.Read(&fsr, sizeof(filestatusrec));
      }
    }
    fileAttach.Close();
  }
}

static std::string fixup_user_entered_email(const std::string& s) {
  auto user_input = s;
  const auto at_pos = user_input.find('@');
  if (at_pos != std::string::npos && at_pos < user_input.size() - 1 &&
      isalpha(user_input.at(at_pos + 1))) {
    if (user_input.find(INTERNET_EMAIL_FAKE_OUTBOUND_ADDRESS) != std::string::npos) {
      StringLowerCase(&user_input);
      user_input += INTERNET_EMAIL_FAKE_OUTBOUND_ADDRESS;
    }
  }
  return user_input;
}

void readmail(int mode) {
  int i, i1, i2, curmail = 0, delme;
  uint8_t nn = 0;
  bool done, okmail;
  char s2[81], *ss2, mnu[81];
  mailrec m{};
  mailrec m1{};
  char ch;
  int num_mail, num_mail1;
  net_system_list_rec* csne;
  filestatusrec fsr{};
  bool attach_exists = false;
  bool found = false;
  long l1;

  a()->emchg_ = false;

  bool next = false, abort = false;
  std::vector<tmpmailrec> mloc;
  write_inst(INST_LOC_RMAIL, 0, INST_FLAGS_NONE);
  auto sl = a()->effective_slrec();
  int mw = 0;
  {
    auto pFileEmail(OpenEmailFile(false));
    if (!pFileEmail->IsOpen()) {
      bout << "\r\n\nNo mail file exists!\r\n\n";
      return;
    }
    auto mfl = static_cast<File::size_type>(pFileEmail->length() / sizeof(mailrec));
    for (i = 0; i < mfl && mw < MAXMAIL; i++) {
      pFileEmail->Seek(i * sizeof(mailrec), File::Whence::begin);
      pFileEmail->Read(&m, sizeof(mailrec));
      if (m.tosys == 0 && m.touser == a()->usernum) {
        tmpmailrec r = {};
        r.index = static_cast<int16_t>(i);
        r.fromsys = m.fromsys;
        r.fromuser = m.fromuser;
        r.daten = m.daten;
        r.msg = m.msg;
        mloc.emplace_back(r);
        mw++;
      }
    }
    pFileEmail->Close();
  }
  a()->user()->SetNumMailWaiting(mw);
  if (mloc.empty()) {
    bout << "\r\n\n|#3You have no mail.\r\n\n";
    return;
  }
  if (mloc.size() == 1) {
    curmail = 0;
  } else {
    bout << "\r\n\n|#7You have mail from:\r\n\n";

    if ((a()->user()->GetScreenChars() >= 80) && a()->mail_who_field_len) {
      std::string left = okansi() ? "\xC1\xC2\xC4" : "++-";
      std::ostringstream ss;
      ss << "|#7" << std::string(4, left[2]) << left[1]
         << std::string(a()->mail_who_field_len - 4, left[2]) << std::string(1, left[1])
         << std::string(a()->user()->GetScreenChars() - a()->mail_who_field_len - 3, left[2]);
      bout.bpla(ss.str(), &abort);
    }
    for (i = 0; (i < mw && !abort); i++) {
      if (!read_same_email(mloc, mw, i, m, 0, 0)) {
        continue;
      }
      if (mode == 1 && (m.status & status_seen)) {
        ++curmail;
        continue;
      }

      net_networks_rec net{};
      nn = network_number_from(&m);
      if (nn <= a()->nets().size()) {
        net = a()->nets()[nn];
      } else {
        net.sysnum = -1;
        net.type = network_type_t::wwivnet;
        net.name = fmt::format("<deleted network #{}>", nn);
        nn = 255;
      }
      std::ostringstream ss(std::ostringstream::ate);
      ss << "|#2" << fmt::sprintf("%3d", i + 1) << (m.status & status_seen ? " " : "|#3*")
         << (okansi() ? '\xB3' : '|') << "|#1 ";

      if (m.anony & anony_sender && (sl.ability & ability_read_email_anony) == 0) {
        ss << ">UNKNOWN<";
      } else {
        if (m.fromsys == 0) {
          if (m.fromuser == 65535) {
            if (nn != 255) {
              ss << net.name;
            }
          } else {
            ss << a()->names()->UserName(m.fromuser);
          }
        } else {
          set_net_num(nn);
          csne = next_system(m.fromsys);
          string system_name;
          if (csne) {
            system_name = csne->name;
          } else {
            system_name = "Unknown System";
          }
          std::string s1;
          if (nn == 255) {
            s1 = fmt::format("#{} @{}.", m.fromuser, m.fromsys, net.name);
          } else {
            if (auto o = readfile(&m.msg, "email")) {
              // we know b is much bigger than s2, so don't
              // use to_char_array
              strncpy(s2, o.value().c_str(), sizeof(s2) - 1);
              ss2 = strtok(s2, "\r");
              if (m.fromsys == INTERNET_EMAIL_FAKE_OUTBOUND_NODE ||
                  m.fromsys == FTN_FAKE_OUTBOUND_NODE) {
                s1 = stripcolors(ss2);
              } else {
                s1 = fmt::format("{} {}@{}.{} ({})", stripcolors(strip_to_node(ss2)), m.fromuser,
                                 m.fromsys, net.name, system_name);
              }
              if (s1.size() > a()->mail_who_field_len) {
                s1.resize(a()->mail_who_field_len);
              }
            } else {
              if (ssize(a()->nets()) > 1) {
                s1 = fmt::format("#{} @{}.{} ({})", m.fromuser, m.fromsys,
                                 net.name, system_name);
              } else {
                s1 = fmt::format("#{} @{} ({})", m.fromuser, m.fromsys, system_name);
              }
            }
          }
          ss << s1;
        }
      }

      if (a()->user()->GetScreenChars() >= 80 && a()->mail_who_field_len) {
        const auto current_s = ss.str();
        if (size_without_colors(current_s) > a()->mail_who_field_len) {
          // resize ss to trim it since it's larger than mail_who_field_len
          trim_to_size_ignore_colors(current_s, a()->mail_who_field_len);
          ss.str(current_s);
        }
        const auto siz = a()->mail_who_field_len + 1 - size_without_colors(current_s);
        ss << std::string(std::max(0, siz), ' ');
        if (okansi()) {
          ss << "|#7\xB3|#1";
        } else {
          ss << "|";
        }
        ss << " " << stripcolors(m.title);
      }
      const auto current_line =
          trim_to_size_ignore_colors(ss.str(), a()->user()->GetScreenChars() - 1);
      bout.bpla(current_line, &abort);

      if (i == mw - 1 && a()->user()->GetScreenChars() >= 80 && !abort &&
          a()->mail_who_field_len) {
        std::string s1 = okansi() ? "\xC1\xC3\xC4" : "++-";
        std::ostringstream ss1;
        ss1 << "|#7" << std::string(4, s1[2]) << s1[0];
        ss1 << std::string(a()->mail_who_field_len - 4, s1[2]);
        ss1 << std::string(1, s1[0]);
        ss1 << std::string(a()->user()->GetScreenChars() - a()->mail_who_field_len - 3, s1[2]);
        bout.bpla(ss1.str(), &abort);
      }
    }
    bout.nl();
    bout
        << "|#9(|#2Q|#9=|#2Quit|#9, |#2Enter|#9=|#2First Message|#9) \r\n|#9Enter message number: ";
    auto user_selection = input(3, true);
    if (contains(user_selection, 'Q')) {
      return;
    }
    i = to_number<int>(user_selection);
    if (i) {
      if (!mode) {
        if (i <= mw) {
          curmail = i - 1;
        } else {
          curmail = 0;
        }
      }
    }
  }
  done = false;

  do {
    abort = false;
    bout.nl(2);
    next = false;
    string title = m.title;

    if (!read_same_email(mloc, mw, curmail, m, 0, 0)) {
      title += ">>> MAIL DELETED <<<";
      okmail = false;
      bout.nl(3);
    } else {
      strcpy(a()->context().irt_, m.title);
      abort = false;
      i = ((ability_read_email_anony & sl.ability) != 0);
      okmail = true;
      if (m.fromsys && !m.fromuser) {
        grab_user_name(&(m.msg), "email", network_number_from(&m));
      } else {
        a()->net_email_name.clear();
      }
      if (m.status & status_source_verified) {
        int sv_type = source_verfied_type(&m);
        if (sv_type > 0) {
          title += StrCat("-=> Source Verified Type ", sv_type);
          if (sv_type == 1) {
            title += " (From NC)";
          } else if (sv_type > 256 && sv_type < 512) {
            title += StrCat(" (From GC-", sv_type - 256, ")");
          }
        } else {
          title += "-=> Source Verified (unknown type)";
        }
      }
      nn = network_number_from(&m);
      set_net_num(nn);
      int nFromSystem = 0;
      int nFromUser = 0;
      if (nn != 255) {
        nFromSystem = m.fromsys;
        nFromUser = m.fromuser;
      }
      if (!abort) {
        // read_type2_message will parse out the title and other fields of the
        // message, including sender name (which is all we have for FTN messages).
        // We need to get the full header before that and pass it into this
        // method to display it.
        auto msg = read_type2_message(&m.msg, m.anony & 0x0f, i ? true : false, "email",
                                      nFromSystem, nFromUser);
        msg.message_area = "Personal E-Mail";
        msg.title = m.title;
        msg.message_number = curmail + 1;
        msg.total_messages = mw;
        // We set this to false since we do *not* want to use the
        // command handling from the full screen reader.
        msg.use_msg_command_handler = false;
        if (a()->current_net().type == network_type_t::ftn) {
          // Set email name to be the to address.
          // This is also done above in grab_user_name, but we should stop using
          // a()->net_email_name
          a()->net_email_name = msg.from_user_name;
        }
        display_type2_message(msg, &next);
        if (!(m.status & status_seen)) {
          read_same_email(mloc, mw, curmail, m, 0, status_seen);
        }
      }
      found = false;
      attach_exists = false;
      if (m.status & status_file) {
        File fileAttach(FilePath(a()->config()->datadir(), ATTACH_DAT));
        if (fileAttach.Open(File::modeBinary | File::modeReadOnly)) {
          l1 = fileAttach.Read(&fsr, sizeof(fsr));
          while (l1 > 0 && !found) {
            if (m.daten == static_cast<uint32_t>(fsr.id)) {
              found = true;
              if (File::Exists(FilePath(a()->GetAttachmentDirectory(), fsr.filename))) {
                bout << "'T' to download attached file \"" << fsr.filename << "\" (" << fsr.numbytes
                     << " bytes).\r\n";
                attach_exists = true;
              } else {
                bout << "Attached file \"" << fsr.filename << "\" (" << fsr.numbytes
                     << " bytes) is missing!\r\n";
              }
            }
            if (!found) {
              l1 = fileAttach.Read(&fsr, sizeof(fsr));
            }
          }
          if (!found) {
            bout << "File attached but attachment data missing.  Alert sysop!\r\n";
          }
        } else {
          bout << "File attached but attachment data missing.  Alert sysop!\r\n";
        }
        fileAttach.Close();
      }
    }

    do {
      string allowable;
      write_inst(INST_LOC_RMAIL, 0, INST_FLAGS_NONE);
      set_net_num(nn);
      auto& net = a()->nets()[nn];
      i1 = 1;
      if (!a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
        strcpy(mnu, EMAIL_NOEXT);
        bout << "|#2Mail {?} : ";
      }
      if (so()) {
        strcpy(mnu, SY_EMAIL_NOEXT);
        if (a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
          bout << "|#2Mail |#7{|#1QSRIDAF?-+GEMZPVUOLCNY@|#7} |#2: ";
        }
        allowable = "QSRIDAF?-+GEMZPVUOLCNY@";
      } else {
        if (cs()) {
          strcpy(mnu, CS_EMAIL_NOEXT);
          if (a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
            bout << "|#2Mail |#7{|#1QSRIDAF?-+GZPVUOCY@|#7} |#2: ";
          }
          allowable = "QSRIDAF?-+GZPVUOCY@";
        } else {
          if (!okmail) {
            strcpy(mnu, RS_EMAIL_NOEXT);
            if (a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
              bout << "|#2Mail |#7{|#1QI?-+GY|#7} |#2: ";
            }
            allowable = "QI?-+G";
          } else {
            strcpy(mnu, EMAIL_NOEXT);
            if (a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
              bout << "|#2Mail |#7{|#1QSRIDAF?+-GY@|#7} |#2: ";
            }
            allowable = "QSRIDAF?-+GY@";
          }
        }
      }
      if ((m.status & status_file) && found && attach_exists) {
        if (a()->HasConfigFlag(OP_FLAGS_MAIL_PROMPT)) {
          bout << "\b\b|#7{|#1T|#7} |#2: |#0";
        }
        allowable += "T";
      }
      ch = onek(allowable);
      if (okmail && !read_same_email(mloc, mw, curmail, m, 0, 0)) {
        bout << "\r\nMail got deleted.\r\n\n";
        ch = 'R';
      }
      delme = 0;
      switch (ch) {
      case 'T': {
        bout.nl();
        auto fn = FilePath(a()->GetAttachmentDirectory(), fsr.filename);
        bool sentt;
        bool abortt;
        send_file(fn.string(), &sentt, &abortt, fsr.filename, -1, fsr.numbytes);
        if (sentt) {
          bout << "\r\nAttached file sent.\r\n";
          sysoplog() << fmt::sprintf("Downloaded %ldk of attached file %s.",
                                     (fsr.numbytes + 1023) / 1024, fsr.filename);
        } else {
          bout << "\r\nAttached file not completely sent.\r\n";
          sysoplog() << fmt::sprintf("Tried to download attached file %s.", fsr.filename);
        }
        bout.nl();
      } break;
      case 'N':
        if (m.fromuser == 1) {
          add_netsubscriber(net, nn, m.fromsys);
        } else {
          add_netsubscriber(net, nn, 0);
        }
        break;
      case 'E':
        if (so() && okmail) {
          if (auto o = readfile(&(m.msg), "email")) {
            auto b = o.value();
            extract_out(b, m.title);
          }
        }
        i1 = 0;
        break;
      case 'Q':
        done = true;
        break;
      case 'O': {
        if (cs() && okmail && m.fromuser != 65535 && nn != 255) {
          show_files("*.frm", a()->config()->gfilesdir().c_str());
          bout << "|#2Which form letter: ";
          auto user_input = input(8, true);
          if (user_input.empty()) {
            break;
          }
          auto fn = FilePath(a()->config()->gfilesdir(), StrCat(user_input, ".frm"));
          if (!File::Exists(fn)) {
            fn = FilePath(a()->config()->gfilesdir(), StrCat("form", user_input, ".msg"));
          }
          if (File::Exists(fn)) {
            LoadFileIntoWorkspace(fn, true);
            num_mail = a()->user()->GetNumFeedbackSent() + a()->user()->GetNumEmailSent() +
                       a()->user()->GetNumNetEmailSent();
            clear_quotes();
            if (m.fromuser != 65535) {
              email(m.title, m.fromuser, m.fromsys, false, m.anony);
            }
            num_mail1 = static_cast<long>(a()->user()->GetNumFeedbackSent()) +
                        static_cast<long>(a()->user()->GetNumEmailSent()) +
                        static_cast<long>(a()->user()->GetNumNetEmailSent());
            if (num_mail != num_mail1) {
              const string userandnet =
                  a()->names()->UserName(a()->usernum, a()->current_net().sysnum);
              string msg;
              if (m.fromsys != 0) {
                msg = StrCat(a()->network_name(), ": ", userandnet);
              } else {
                msg = userandnet;
              }
              if (m.anony & anony_receiver) {
                msg += ">UNKNOWN<";
              }
              msg += " read your mail on ";
              msg += fulldate();
              if (!(m.status & status_source_verified)) {
                ssm(m.fromuser, m.fromsys, &net) << msg;
              }
              read_same_email(mloc, mw, curmail, m, 1, 0);
              ++curmail;
              if (curmail >= mw) {
                done = true;
              }
            } else {
              // need instance
              File::Remove(FilePath(a()->temp_directory(), INPUT_MSG));
            }
          } else {
            bout << "\r\nFile not found.\r\n\n";
            i1 = 0;
          }
        }
      } break;
      case 'G': {
        bout << "|#2Go to which (1-" << mw << ") ? |#0";
        auto user_input = input(3);
        i1 = to_number<int>(user_input);
        if (i1 > 0 && i1 <= mw) {
          curmail = i1 - 1;
          i1 = 1;
        } else {
          i1 = 0;
        }
      } break;
      case 'I':
      case '+':
        ++curmail;
        if (curmail >= mw) {
          done = true;
        }
        break;
      case '-':
        if (curmail) {
          --curmail;
        }
        break;
      case 'R':
        break;
      case '?':
        printfile(mnu);
        i1 = 0;
        break;
      case 'M':
        if (!okmail) {
          break;
        }
        if (so()) {
          if (!a()->context().IsUserOnline()) {
            a()->set_current_user_sub_num(0);
            a()->context().SetCurrentReadMessageArea(0);
            a()->context().set_current_user_sub_conf_num(0);
          }
          tmp_disable_conf(true);
          bout.nl();
          string ss1;
          do {
            bout << "|#2Move to which sub? ";
            ss1 = mmkey(MMKeyAreaType::subs);
            if (ss1[0] == '?') {
              old_sublist();
            }
          } while ((!a()->context().hangup()) && (ss1[0] == '?'));
          i = -1;
          if ((ss1[0] == 0) || a()->context().hangup()) {
            i1 = 0;
            bout.nl();
            tmp_disable_conf(false);
            break;
          }
          for (i1 = 0; (i1 < ssize(a()->subs().subs())) && (a()->usub[i1].subnum != -1); i1++) {
            if (ss1 == a()->usub[i1].keys) {
              i = i1;
            }
          }
          if (i != -1) {
            if (a()->effective_sl() < a()->subs().sub(a()->usub[i].subnum).postsl) {
              bout << "\r\nSorry, you don't have post access on that sub.\r\n\n";
              i = -1;
            }
          }
          if (i != -1) {
            auto o = readfile(&(m.msg), "email");
            if (!o) {
              break;
            }
            auto b = o.value();

            postrec p{};
            strcpy(p.title, m.title);
            p.anony = m.anony;
            p.ownersys = m.fromsys;
            a()->SetNumMessagesInCurrentMessageArea(p.owneruser);
            p.owneruser = static_cast<uint16_t>(a()->usernum);
            p.msg = m.msg;
            p.daten = m.daten;
            p.status = 0;

            iscan(i);
            open_sub(true);
            if (!a()->current_sub().nets.empty()) {
              p.status |= status_pending_net;
            }
            p.msg.storage_type = static_cast<uint8_t>(a()->current_sub().storage_type);
            savefile(b, &(p.msg), a()->current_sub().filename);
            auto status = a()->status_manager()->BeginTransaction();
            p.qscan = status->IncrementQScanPointer();
            a()->status_manager()->CommitTransaction(std::move(status));
            if (a()->GetNumMessagesInCurrentMessageArea() >= a()->current_sub().maxmsgs) {
              i1 = 1;
              i2 = 0;
              while (i2 == 0 && i1 <= a()->GetNumMessagesInCurrentMessageArea()) {
                if ((get_post(i1)->status & status_no_delete) == 0) {
                  i2 = i1;
                }
                ++i1;
              }
              if (i2 == 0) {
                i2 = 1;
              }
              delete_message(i2);
            }
            add_post(&p);
            status = a()->status_manager()->BeginTransaction();
            status->IncrementNumMessagesPostedToday();
            status->IncrementNumLocalPosts();
            a()->status_manager()->CommitTransaction(std::move(status));
            close_sub();
            tmp_disable_conf(false);
            iscan(a()->current_user_sub_num());
            bout << "\r\n\n|#9Message moved.\r\n\n";
            auto temp_num_msgs = a()->GetNumMessagesInCurrentMessageArea();
            resynch(&temp_num_msgs, &p);
            a()->SetNumMessagesInCurrentMessageArea(temp_num_msgs);
          } else {
            tmp_disable_conf(false);
          }
        } break;
      case 'D': {
        string message;
        if (!okmail) {
          break;
        }
        bout << "|#5Delete this message? ";
        if (!noyes()) {
          break;
        }
        if (m.fromsys != 0) {
          message = StrCat(a()->network_name(), ": ",
                           a()->names()->UserName(a()->usernum, a()->current_net().sysnum));
        } else {
          message = a()->names()->UserName(a()->usernum, a()->current_net().sysnum);
        }

        if (m.anony & anony_receiver) {
          message = ">UNKNOWN<";
        }

        message += " read your mail on ";
        message += fulldate();
        if (!(m.status & status_source_verified) && nn != 255) {
          ssm(m.fromuser, m.fromsys, &net) << message;
        }
      } 
      [[fallthrough]];
      case 'Z':
        if (!okmail) {
          break;
        }
        read_same_email(mloc, mw, curmail, m, 1, 0);
        ++curmail;
        if (curmail >= mw) {
          done = true;
        }
        found = false;
        if (m.status & status_file) {
          delete_attachment(m.daten, 1);
        }
        break;
      case 'P':
        if (!okmail) {
          break;
        }
        purgemail(mloc, mw, &curmail, &m, &sl);
        if (curmail >= mw) {
          done = true;
        }
        break;
      case 'F': {
        if (!okmail) {
          break;
        }
        if (m.status & status_multimail) {
          bout << "\r\nCan't forward multimail.\r\n\n";
          break;
        }
        bout.nl(2);
        if (okfsed() && a()->user()->IsUseAutoQuote()) {
          // TODO: optimize this since we also call readfile in grab_user_name
          auto reply_to_name = grab_user_name(&(m.msg), "email", network_number_from(&m));
          if (auto o = readfile(&(m.msg), "email")) {
            auto_quote(o.value(), reply_to_name, quote_date_format_t::forward, m.daten);
            send_email();
          }
          break;
        }

        bout << "|#2Forward to: ";
        auto user_input = fixup_user_entered_email(input(75));
        auto [un, sn] = parse_email_info(user_input);
        if (un || sn) {
          if (ForwardMessage(&un, &sn)) {
            bout << "Mail forwarded.\r\n";
          }
          if (un == a()->usernum && sn == 0 && !cs()) {
            bout << "Can't forward to yourself.\r\n";
            un = 0;
          }
          if (un || sn) {
            std::string fwd_email_name;
            if (sn) {
              if (sn == 1 && un == 0 &&
                  a()->current_net().type == network_type_t::internet) {
                fwd_email_name = a()->net_email_name;
              } else {
                auto netname = ssize(a()->nets()) > 1 ? a()->network_name() : "";
                fwd_email_name = username_system_net_as_string(un, a()->net_email_name,
                                                                sn, netname);
              }
            } else {
              set_net_num(nn);
              fwd_email_name = a()->names()->UserName(un, a()->current_net().sysnum);
            }
            if (ok_to_mail(un, sn, false)) {
              bout << "|#5Forward to " << fwd_email_name << "? ";
              if (yesno()) {
                auto file(OpenEmailFile(true));
                if (!file->IsOpen()) {
                  break;
                }
                file->Seek(mloc[curmail].index * sizeof(mailrec), File::Whence::begin);
                file->Read(&m, sizeof(mailrec));
                if (!same_email(mloc[curmail], m)) {
                  bout << "Error, mail moved.\r\n";
                  break;
                }
                bout << "|#5Delete this message? ";
                if (yesno()) {
                  if (m.status & status_file) {
                    delete_attachment(m.daten, 0);
                  }
                  delme = 1;
                  m1.touser = 0;
                  m1.tosys = 0;
                  m1.daten = 0xffffffff;
                  m1.msg.storage_type = 0;
                  m1.msg.stored_as = 0xffffffff;
                  file->Seek(mloc[curmail].index * sizeof(mailrec), File::Whence::begin);
                  file->Write(&m1, sizeof(mailrec));
                } else {
                  string b;
                  if (auto o = readfile(&(m.msg), "email")) {
                    savefile(o.value(), &(m.msg), "email");
                  }
                }
                m.status |= status_forwarded;
                m.status |= status_seen;
                file->Close();

                i = a()->net_num();
                const auto fwd_user_name =
                    a()->names()->UserName(a()->usernum, a()->current_net().sysnum);
                auto s = fmt::sprintf("\r\nForwarded to %s from %s.", fwd_email_name, fwd_user_name);

                set_net_num(nn);
                net = a()->nets()[nn];
                lineadd(&m.msg, s, "email");
                s = StrCat(fwd_user_name, " forwarded your mail to ", fwd_email_name);
                if (!(m.status & status_source_verified)) {
                  ssm(m.fromuser, m.fromsys, &net) << s;
                }
                set_net_num(i);
                s = StrCat("Forwarded mail to ", fwd_email_name);
                if (delme) {
                  a()->user()->SetNumMailWaiting(a()->user()->GetNumMailWaiting() - 1);
                }
                bout << "Forwarding: ";
                ::EmailData email;
                email.title = m.title;
                email.msg = &m.msg;
                email.anony = m.anony;
                email.user_number = un;
                email.system_number = sn;
                email.an = true;
                email.forwarded_code = delme; // this looks totally wrong to me...
                email.silent_mode = false;

                if (nn != 255 && nn == a()->net_num()) {
                  email.from_user = m.fromuser;
                  email.from_system = m.fromsys ? m.fromsys : a()->nets()[nn].sysnum;
                  email.from_network_number = nn;
                  sendout_email(email);
                } else {
                  email.from_user = a()->usernum;
                  email.from_system = a()->current_net().sysnum;
                  email.from_network_number = a()->net_num();
                  sendout_email(email);
                }
                ++curmail;
                if (curmail >= mw) {
                  done = true;
                }
              }
            }
          }
        }
        delme = 0;
      } break;
      case 'A':
      case 'S':
      case '@': {
        if (!okmail) {
          break;
        }
        string reply_to_name;
        num_mail = static_cast<long>(a()->user()->GetNumFeedbackSent()) +
                   static_cast<long>(a()->user()->GetNumEmailSent()) +
                   static_cast<long>(a()->user()->GetNumNetEmailSent());
        if (nn == 255) {
          bout << "|#6Deleted network.\r\n";
          i1 = 0;
          break;
        }
        if (m.fromuser != 65535) {
          // TODO: optimize this since we also call readfile in grab_user_name
          reply_to_name = grab_user_name(&(m.msg), "email", network_number_from(&m));
          if (auto o = readfile(&(m.msg), "email")) {
            if (okfsed() && a()->user()->IsUseAutoQuote()) {
              // used to be 1 or 2 depending on s[0] == '@', but
              // that's allowable now and @ was never in the beginning.
              auto_quote(o.value(), reply_to_name, quote_date_format_t::email, m.daten);
            }
            grab_quotes(o.value(), reply_to_name);
          }

          if (ch == '@') {
            bout << "\r\n|#9Enter user name or number:\r\n:";
            auto user_email = fixup_user_entered_email(input(75, true));
            auto [un, sy] = parse_email_info(user_email);
            if (un || sy) {
              email("", un, sy, false, 0);
            }
          } else {
            email("", m.fromuser, m.fromsys, false, m.anony);
          }
          clear_quotes();
        }
        num_mail1 = static_cast<long>(a()->user()->GetNumFeedbackSent()) +
                    static_cast<long>(a()->user()->GetNumEmailSent()) +
                    static_cast<long>(a()->user()->GetNumNetEmailSent());
        if (ch == 'A' || ch == '@') {
          if (num_mail != num_mail1) {
            string message;
            const string name = a()->names()->UserName(a()->usernum, a()->current_net().sysnum);
            if (m.fromsys != 0) {
              message = a()->network_name();
              message += ": ";
              message += name;
            } else {
              message = name;
            }
            if (m.anony & anony_receiver) {
              message = ">UNKNOWN<";
            }
            message += " read your mail on ";
            message += fulldate();
            if (!(m.status & status_source_verified)) {
              ssm(m.fromuser, m.fromsys, &net) << message;
            }
            read_same_email(mloc, mw, curmail, m, 1, 0);
            ++curmail;
            if (curmail >= mw) {
              done = true;
            }
            found = false;
            if (m.status & status_file) {
              delete_attachment(m.daten, 0);
            }
          } else {
            bout << "\r\nNo mail sent.\r\n\n";
            i1 = 0;
          }
        } else {
          if (num_mail != num_mail1) {
            if (!(m.status & status_replied)) {
              read_same_email(mloc, mw, curmail, m, 0, status_replied);
            }
            ++curmail;
            if (curmail >= mw) {
              done = true;
            }
          }
        }
      } break;
      case 'U':
      case 'V':
      case 'C':
        if (!okmail) {
          break;
        }
        if (m.fromsys == 0 && cs() && m.fromuser != 65535) {
          if (ch == 'V') {
            valuser(m.fromuser);
          }
        } else if (cs()) {
          bout << "\r\nMail from another system.\r\n\n";
        }
        i1 = 0;
        break;
      case 'L': {
        if (!so()) {
          break;
        }
        bout << "\r\n|#2Filename: ";
        string fileName = input_path(50);
        if (!fileName.empty()) {
          bout.nl();
          bout << "|#5Allow editing? ";
          if (yesno()) {
            bout.nl();
            LoadFileIntoWorkspace(fileName, false);
          } else {
            bout.nl();
            LoadFileIntoWorkspace(fileName, true);
          }
        }
      } break;
      case 'Y': // Add from here
        if (curmail >= 0) {
          auto o = readfile(&(m.msg), "email");
          if (!o) {
            break;
          }
          auto b = o.value();
          bout << "E-mail download -\r\n\n|#2Filename: ";
          auto downloadFileName = input(12);
          if (!okfn(downloadFileName)) {
            break;
          }
          const auto fn = FilePath(a()->temp_directory(), downloadFileName);
          File::Remove(fn);
          TextFile tf(fn, "w");
          tf.Write(b);
          tf.Close();
          bool bSent;
          bool bAbort;
          send_file(fn.string(), &bSent, &bAbort, fn.string(), -1, b.size());
          if (bSent) {
            bout << "E-mail download successful.\r\n";
            sysoplog() << "Downloaded E-mail";
          } else {
            bout << "E-mail download aborted.\r\n";
          }
        }
        break;
      }
    } while (!i1 && !a()->context().hangup());
  } while (!a()->context().hangup() && !done);
}

int check_new_mail(int user_number) {
  auto new_messages = 0; // number of new mail

  auto pFileEmail(OpenEmailFile(false));
  if (pFileEmail->Exists() && pFileEmail->IsOpen()) {
    const auto mfLength = static_cast<int>(pFileEmail->length() / sizeof(mailrec));
    for (auto i = 0; i < mfLength; i++) {
      mailrec m{};
      pFileEmail->Seek(i * sizeof(mailrec), File::Whence::begin);
      pFileEmail->Read(&m, sizeof(mailrec));
      if (m.tosys == 0 && m.touser == user_number) {
        if (!(m.status & status_seen)) {
          ++new_messages;
        }
      }
    }
    pFileEmail->Close();
  }
  return new_messages;
}
