/**
 * AIS-specific JS code
 */

(function (){
    var loader = new YAHOO.util.YUILoader({ 
        base: "build", 
        require: ["animation","autocomplete","connection","datasource","json"
         	//",logger"
 			//",paginator"
        ], 
        loadOptional: false, 
        combine: true, 
        filter: "MIN", 
        allowRollup: true 
    }); 
 	loader.insert({

  		onSuccess: function(){
  	
    		//var log = new YAHOO.widget.LogReader(); 	//create logger at the right (remove me in prod!)

//////Start custom code

 

//////End custom code

 		}//onSuccess
        
 	});//insert
})();



