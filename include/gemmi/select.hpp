// Copyright 2018 Global Phasing Ltd.
//
// Selections.

#ifndef GEMMI_SELECT_HPP_
#define GEMMI_SELECT_HPP_

#include <string>
#include <cstdlib>   // for strtol
#include <cctype>    // for isalpha
#include <climits>   // for INT_MIN, INT_MAX
#include "fail.hpp"  // for fail
#include "model.hpp" // for Model, Chain, etc
#include "iterator.hpp" // for FilterProxy

namespace gemmi {

// from http://www.ccp4.ac.uk/html/pdbcur.html
// Specification of the selection sets:
// either
//     /mdl/chn/s1.i1-s2.i2/at[el]:aloc
// or
//     /mdl/chn/*(res).ic/at[el]:aloc
//

struct Selection {
  struct List {
    bool all = true;
    bool inverted = false;
    std::string list;  // comma-separated

    std::string str() const {
      if (all)
        return "*";
      return inverted ? "!" + list : list;
    }
  };

  struct SequenceId {
    int seqnum;
    char icode;

    std::string str() const {
      std::string s;
      if (seqnum != INT_MIN && seqnum != INT_MAX)
        s = std::to_string(seqnum);
      if (icode != '*') {
        s += '.';
        if (icode != ' ')
          s += icode;
      }
      return s;
    }

    int compare(const SeqId& seqid) const {
      if (seqnum != *seqid.num)
        return seqnum < *seqid.num ? -1 : 1;
      if (icode != '*' && icode != seqid.icode)
        return icode < seqid.icode ? -1 : 1;
      return 0;
    }
  };

  int mdl = 0;            // 0 = all
  List chain_ids;
  SequenceId from_seqid = {INT_MIN, '*'};
  SequenceId to_seqid = {INT_MAX, '*'};
  List residue_names;
  List atom_names;
  List elements;
  List altlocs;

  std::string to_cid() const {
    std::string cid(1, '/');
    if (mdl != 0)
      cid += std::to_string(mdl);
    cid += '/';
    cid += chain_ids.str();
    cid += '/';
    cid += from_seqid.str();
    if (!residue_names.all) {
      cid += residue_names.str();
    } else {
      cid += '-';
      cid += to_seqid.str();
    }
    cid += '/';
    cid += atom_names.str();
    if (!elements.all)
      cid += "[" + elements.str() + "]";
    if (!altlocs.all)
      cid += ":" + altlocs.str();
    return cid;
  }

  bool matches(const gemmi::Model& model) const {
    return mdl == 0 || std::to_string(mdl) == model.name;
  }
  bool matches(const gemmi::Chain& chain) const {
    return chain_ids.all || find_in_list(chain.name, chain_ids);
  }
  bool matches(const gemmi::Residue& res) const {
    return (residue_names.all || find_in_list(res.name, residue_names)) &&
            from_seqid.compare(res.seqid) <= 0 &&
            to_seqid.compare(res.seqid) >= 0;
  }
  bool matches(const gemmi::Atom& a) const {
    return (atom_names.all || find_in_list(a.name, atom_names)) &&
           (elements.all || find_in_list(a.element.uname(), elements)) &&
           (altlocs.all ||
            find_in_list(std::string(a.altloc ? 0 : 1, a.altloc), altlocs));
  }
  bool matches(const gemmi::CRA& cra) const {
    return (cra.chain == nullptr || matches(*cra.chain)) &&
           (cra.residue == nullptr || matches(*cra.residue)) &&
           (cra.atom == nullptr || matches(*cra.atom));
  }

  FilterProxy<Selection, Model> models(Structure& st) const {
    return {*this, st.models};
  }
  FilterProxy<Selection, Chain> chains(Model& model) const {
    return {*this, model.chains};
  }
  FilterProxy<Selection, Residue> residues(Chain& chain) const {
    return {*this, chain.residues};
  }
  FilterProxy<Selection, Atom> atoms(Residue& residue) const {
    return {*this, residue.atoms};
  }

  CRA first_in_model(Model& model) const {
    if (matches(model))
      for (Chain& chain : model.chains) {
        if (matches(chain))
          for (Residue& res : chain.residues) {
            if (matches(res))
              for (Atom& atom : res.atoms) {
                if (matches(atom))
                  return {&chain, &res, &atom};
              }
          }
      }
    return {nullptr, nullptr, nullptr};
  }

  std::pair<Model*, CRA> first(Structure& st) const {
    for (Model& model : st.models) {
      CRA cra = first_in_model(model);
      if (cra.chain)
        return {&model, cra};
    }
    return {nullptr, {nullptr, nullptr, nullptr}};
  }

private:
  static bool find_in_comma_separated_string(const std::string& name,
                                             const std::string& str) {
    if (name.length() >= str.length())
      return name == str;
    for (size_t start=0, end=0; end != std::string::npos; start=end+1) {
      end = str.find(',', start);
      if (str.compare(start, end - start, name) == 0)
        return true;
    }
    return false;
  }

  // assumes that list.all is checked before this function is called
  static bool find_in_list(const std::string& name, const List& list) {
    bool found = find_in_comma_separated_string(name, list.list);
    return list.inverted ? !found : found;
  }
};

namespace impl {

int determine_omitted_cid_fields(const std::string& cid) {
  if (cid[0] == '/')
    return 0; // model
  if (std::isdigit(cid[0]) || cid[0] == '.' || cid[0] == '(' || cid[0] == '-')
    return 2; // residue
  size_t sep = cid.find_first_of("/(:[");
  if (sep == std::string::npos || cid[sep] == '/')
    return 1; // chain
  if (cid[sep] == '(')
    return 2; // residue
  return 3;  // atom
}

Selection::List make_cid_list(const std::string& cid, size_t pos, size_t end) {
  Selection::List list;
  list.all = (cid[pos] == '*');
  if (cid[pos] == '!') {
    list.inverted = true;
    ++pos;
  }
  list.list = cid.substr(pos, end - pos);
  return list;
}

Selection::SequenceId parse_cid_seqid(const std::string& cid, size_t& pos,
                                      int default_seqnum) {
  size_t initial_pos = pos;
  int seqnum = default_seqnum;
  char icode = ' ';
  if (cid[pos] == '*') {
    ++pos;
    icode = '*';
  } else if (std::isdigit(cid[pos])) {
    char* endptr;
    seqnum = std::strtol(&cid[pos], &endptr, 10);
    pos = endptr - &cid[0];
  }
  if (cid[pos] == '.')
    ++pos;
  if (initial_pos != pos && (std::isalpha(cid[pos]) || cid[pos] == '*'))
    icode = cid[pos++];
  return {seqnum, icode};
}

} // namespace impl

inline Selection parse_cid(const std::string& cid) {
  Selection sel;
  if (cid.empty() || (cid.size() == 1 && cid[0] == '*'))
    return sel;
  int omit = impl::determine_omitted_cid_fields(cid);
  size_t sep = 0;
  // model
  if (omit == 0) {
    sep = cid.find('/', 1);
    if (sep != 1 && cid[1] != '*') {
      char* endptr;
      sel.mdl = std::strtol(&cid[1], &endptr, 10);
      size_t end_pos = endptr - &cid[0];
      if (end_pos != sep && end_pos != cid.length())
        fail("Expected model number first: " + cid);
    }
  }

  // chain
  if (omit <= 1 && sep != std::string::npos) {
    size_t pos = (sep == 0 ? 0 : sep + 1);
    sep = cid.find('/', pos);
    sel.chain_ids = impl::make_cid_list(cid, pos, sep);
  }

  // residue; MMDB CID syntax: s1.i1-s2.i2 or *(res).ic
  // In gemmi both 14.a and 14a are accepted.
  // *(ALA). and *(ALA) and (ALA). can be used instead of (ALA) for
  // compatibility with MMDB.
  if (omit <= 2 && sep != std::string::npos) {
    size_t pos = (sep == 0 ? 0 : sep + 1);
    if (cid[pos] != '(')
      sel.from_seqid = impl::parse_cid_seqid(cid, pos, INT_MIN);
    if (cid[pos] == '(') {
      ++pos;
      size_t right_br = cid.find(')', pos);
      sel.residue_names = impl::make_cid_list(cid, pos, right_br);
      pos = right_br + 1;
    }
    // allow "(RES)." and "(RES).*" and "(RES)*"
    if (cid[pos] == '.')
      ++pos;
    if (cid[pos] == '*')
      ++pos;
    if (cid[pos] == '-') {
      ++pos;
      sel.to_seqid = impl::parse_cid_seqid(cid, pos, INT_MAX);
    }
    sep = pos;
  }

  // atom;  at[el]:aloc
  if (sep < cid.size()) {
    if (sep != 0 && cid[sep] != '/')
        fail("Invalid selection syntax: " + cid);
    size_t pos = (sep == 0 ? 0 : sep + 1);
    size_t end = cid.find_first_of("[:", pos);
    if (end != pos)
      sel.atom_names = impl::make_cid_list(cid, pos, end);
    if (end != std::string::npos) {
      if (cid[end] == '[') {
        pos = end + 1;
        end = cid.find(']', pos);
        sel.elements = impl::make_cid_list(cid, pos, end);
        sel.elements.list = to_upper(sel.elements.list);
        ++end;
      }
      if (cid[end] == ':')
        sel.altlocs = impl::make_cid_list(cid, end + 1, std::string::npos);
      else if (end < cid.length())
        fail("Invalid selection syntax (after ']'): " + cid);
    }
  }

  return sel;
}

} // namespace gemmi
#endif
