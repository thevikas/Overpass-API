#include "../../template_db/block_backend.h"
#include "../../template_db/random_file.h"
#include "../core/settings.h"
#include "meta_collector.h"
#include "user.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace std;

//-----------------------------------------------------------------------------

void User_Statement::set_attributes(const char **attr)
{
  map< string, string > attributes;
  
  attributes["into"] = "_";
  attributes["uid"] = "";
  attributes["name"] = "";
  
  eval_cstr_array(get_name(), attributes, attr);
  
  output = attributes["into"];
  user_name = attributes["name"];
  user_id = atoll(attributes["uid"].c_str());
  if (((user_id == 0) && (user_name == "")) ||
      ((user_id != 0) && (user_name != "")))
  {
    ostringstream temp;
    temp<<"Exactly one of the two attributes \"name\" and \"uid\" must be set.";
    add_static_error(temp.str());
  }
}

uint32 get_user_id(const string& user_name, Transaction& transaction)
{
  Block_Backend< Uint32_Index, User_Data > user_db
      (transaction.data_index(meta_settings().USER_DATA));
  for (Block_Backend< Uint32_Index, User_Data >::Flat_Iterator
      user_it = user_db.flat_begin(); !(user_it == user_db.flat_end()); ++user_it)
  {
    if (user_it.object().name == user_name)
      return user_it.object().id;
  }
  return 0;
}

void calc_ranges
  (set< pair< Uint32_Index, Uint32_Index > >& node_req,
   set< pair< Uint31_Index, Uint31_Index > >& other_req,
   uint32 user_id, Transaction& transaction)
{
  set< Uint32_Index > req;
  req.insert(user_id);
  
  Block_Backend< Uint32_Index, Uint31_Index > user_db
      (transaction.data_index(meta_settings().USER_INDICES));
  for (Block_Backend< Uint32_Index, Uint31_Index >::Discrete_Iterator
      user_it = user_db.discrete_begin(req.begin(), req.end());
      !(user_it == user_db.discrete_end()); ++user_it)
  {
    if ((user_it.object().val() & 0x80000000) == 0)
      node_req.insert(make_pair(Uint32_Index(user_it.object().val()),
			        Uint32_Index(user_it.object().val() + 0x100)));
    // TODO
  }
}

void User_Statement::forecast()
{
}

void User_Statement::execute(Resource_Manager& rman)
{
  stopwatch.start();
  
  map< Uint32_Index, vector< Node_Skeleton > >& nodes
      (rman.sets()[output].nodes);
  map< Uint31_Index, vector< Way_Skeleton > >& ways
      (rman.sets()[output].ways);
  map< Uint31_Index, vector< Relation_Skeleton > >& relations
      (rman.sets()[output].relations);
  map< Uint31_Index, vector< Area_Skeleton > >& areas
      (rman.sets()[output].areas);

  nodes.clear();
  ways.clear();
  relations.clear();
  areas.clear();

  if (user_name != "")
    user_id = get_user_id(user_name, *rman.get_transaction());
	
  set< pair< Uint32_Index, Uint32_Index > > node_req;
  set< pair< Uint31_Index, Uint31_Index > > other_req;
  calc_ranges(node_req, other_req, user_id, *rman.get_transaction());
  
  stopwatch.stop(Stopwatch::NO_DISK);
  uint nodes_count = 0;
  
  {
    Meta_Collector< Uint32_Index, Node_Skeleton > meta_collector
        (node_req, *rman.get_transaction(), meta_settings().NODES_META);
    Block_Backend< Uint32_Index, Node_Skeleton > nodes_db
        (rman.get_transaction()->data_index(osm_base_settings().NODES));
    for (Block_Backend< Uint32_Index, Node_Skeleton >::Range_Iterator
        it(nodes_db.range_begin
        (Default_Range_Iterator< Uint32_Index >(node_req.begin()),
	 Default_Range_Iterator< Uint32_Index >(node_req.end())));
        !(it == nodes_db.range_end()); ++it)
    {
      if (++nodes_count >= 64*1024)
      {
        nodes_count = 0;
        rman.health_check(*this);
      }
    
      const OSM_Element_Metadata_Skeleton* meta_skel
          = meta_collector.get(it.index(), it.object().id);
      if ((meta_skel) && (meta_skel->user_id == user_id))
        nodes[it.index()].push_back(it.object());
    }
    
    stopwatch.add(Stopwatch::NODES, nodes_db.read_count());
    stopwatch.stop(Stopwatch::NODES);
  }
  
  stopwatch.report(get_name());
  rman.health_check(*this);
}