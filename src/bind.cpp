#include <dplyr.h>

using namespace Rcpp ;
using namespace dplyr ;

template <int RTYPE>
inline bool all_na_impl( const Vector<RTYPE>& x ){
    return all( is_na(x) ).is_true() ; 
}

inline bool all_na( SEXP x ){
    RCPP_RETURN_VECTOR( all_na_impl, x ) ;        
}

template <typename Dots>
List rbind__impl( Dots dots ){
    int ndata = dots.size() ;
    int n = 0 ;
    for( int i=0; i<ndata; i++) {
      DataFrame df = dots[i] ;
      if( df.size() ) n += df.nrows() ;
    }
    std::vector<Collecter*> columns ;
    std::vector<String> names ;
    int k=0 ;
    for( int i=0; i<ndata; i++){
        Rcpp::checkUserInterrupt() ;
        
        DataFrame df = dots[i] ;
        if( !df.size() ) continue ;
            
        DataFrameVisitors visitors( df, df.names() ) ;
        int nrows = df.nrows() ;

        CharacterVector df_names = df.names() ;
        for( int j=0; j<df.size(); j++){
            SEXP source = df[j] ;
            String name = df_names[j] ;

            Collecter* coll = 0;
            size_t index = 0 ;
            for( ; index < names.size(); index++){
                if( name == names[index] ){
                    coll = columns[index] ;
                    break ;
                }
            }
            if( ! coll ){
                coll = collecter( source, n ) ;
                columns.push_back( coll );
                names.push_back(name) ;
            }
            
            if( coll->compatible(source) ){
                // if the current source is compatible, collect
                coll->collect( SlicingIndex( k, nrows), source ) ;

            } else if( coll->can_promote(source) ) {
                // setup a new Collecter
                Collecter* new_collecter = promote_collecter(source, n, coll ) ;

                // import data from this chunk
                new_collecter->collect( SlicingIndex( k, nrows), source ) ;

                // import data from previous collecter
                new_collecter->collect( SlicingIndex(0, k), coll->get() ) ;

                // dispose the previous collecter and keep the new one.
                delete coll ;
                columns[index] = new_collecter ;

            } else if( all_na(source) ) {
                // do nothing, the collecter already initialized data with the
                // right NA 
            } else {
                std::stringstream msg ;
                std::string column_name(name) ;
                msg << "incompatible type ("
                    << "data index: "
                    << (i+1)
                    << ", column: '"
                    << column_name
                    << "', was collecting: "
                    << coll->describe()
                    << " ("
                    << DEMANGLE(*coll)
                    << ")"
                    << ", incompatible with data of type: "
                    << get_single_class(source) ;

                stop( msg.str() ) ;
            }

        }

        k += nrows ;
    }

    int nc = columns.size() ;
    List out(nc) ;
    CharacterVector out_names(nc) ;
    for( int i=0; i<nc; i++){
        out[i] = columns[i]->get() ;
        out_names[i] = names[i] ;
    }
    out.attr( "names" ) = out_names ;
    delete_all( columns ) ;
    set_rownames( out, n );
    out.attr( "class" ) = classes_not_grouped() ;

    return out ;
}

//' @export
//' @rdname rbind
// [[Rcpp::export]]
List rbind_all( StrictListOf<DataFrame, NULL_or_Is<DataFrame> > dots ){
    return rbind__impl(dots) ;
}

// [[Rcpp::export]]
List rbind_list__impl( DotsOf<DataFrame> dots ){
    return rbind__impl(dots) ;
}

template <typename Dots>
List cbind__impl( Dots dots ){
  int n = dots.size() ;
  
  // first check that the number of rows is the same
  DataFrame df = dots[0] ;
  int nrows = df.nrows() ;
  int nv = df.size() ;
  for( int i=1; i<n; i++){
    DataFrame current = dots[i] ;
    if( current.nrows() != nrows ){
      std::stringstream ss ;
      ss << "incompatible number of rows (" 
         << current.size()
         << ", expecting "
         << nrows 
      ;
      stop( ss.str() ) ;
    }
    nv += current.size() ;
  }
  
  // collect columns
  List out(nv) ;
  CharacterVector out_names(nv) ;
  
  // then do the subsequent dfs
  for( int i=0, k=0 ; i<n; i++){
      Rcpp::checkUserInterrupt() ;
    
      DataFrame current = dots[i] ;
      CharacterVector current_names = current.names() ;
      int nc = current.size() ;
      for( int j=0; j<nc; j++, k++){
          out[k] = shared_SEXP(current[j]) ;
          out_names[k] = current_names[j] ;
      }
  }
  out.names() = out_names ;
  set_rownames( out, nrows ) ;
  out.attr( "class") = "data.frame" ;
  return out ;
}

// [[Rcpp::export]]
List cbind_list__impl( DotsOf<DataFrame> dots ){
  return cbind__impl( dots ) ;  
}

// [[Rcpp::export]]
List cbind_all( StrictListOf<DataFrame, NULL_or_Is<DataFrame> > dots ){
    return cbind__impl( dots ) ;  
}

// [[Rcpp::export]]
SEXP combine_all( List data ){
    int nv = data.size() ;
    if( nv == 0 ) stop("combine_all needs at least one vector") ;
    
    // get the size of the output
    int n = 0 ;
    for( int i=0; i<nv; i++){
        n += Rf_length(data[i]) ;    
    }
    
    // collect
    Collecter* coll = collecter( data[0], n ) ;
    coll->collect( SlicingIndex(0, Rf_length(data[0])), data[0] ) ;
    int k = Rf_length(data[0]) ;
    
    for( int i=1; i<nv; i++){
        SEXP current = data[i] ;
        int n_current= Rf_length(current) ;
        if( coll->compatible(current) ){
            coll->collect( SlicingIndex(k, n_current), current ) ;
        } else if( coll->can_promote(current) ) {
            Collecter* new_coll = promote_collecter(current, n, coll) ;
            new_coll->collect( SlicingIndex(k, n_current), current ) ;
            new_coll->collect( SlicingIndex(0, k), coll->get() ) ;
            delete coll ;
            coll = new_coll ;
        } else {
            std::stringstream msg ;
            msg << "incompatible type at index "
                << (i+1)
                << " : "
                << get_single_class(current)
                << ", was collecting : "
                << get_single_class(coll->get())
            ;
            stop( msg.str() ) ;
        }
        k += n_current ;
    }
    
    RObject out = coll->get() ;
    delete coll ;
    return out ;
}

