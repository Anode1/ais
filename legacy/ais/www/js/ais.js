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

    var dataSource = new YAHOO.util.XHRDataSource("main");
    dataSource.responseType = YAHOO.util.XHRDataSource.TYPE_TEXT;
    dataSource.responseSchema = {
        recordDelim: "\n", 
        fieldDelim: " " 
    };
    //myDataSource.maxCacheEntries = 5;

    var component = new YAHOO.widget.AutoComplete("textinput","container", dataSource);
    component.queryMatchContains = true;
    component.queryQuestionMark = false;
    component.prehighlightClassName = "yui-ac-prehighlight";
    component.typeAhead = true;
    component.useShadow = true;
	component.minQueryLength = 2;

    // Keeps container centered:
    component.doBeforeExpandContainer = function(oTextbox, oContainer, sQuery, aResults) {
        var pos = YAHOO.util.Dom.getXY(oTextbox);
        pos[1] += YAHOO.util.Dom.get(oTextbox).offsetHeight + 2;
        YAHOO.util.Dom.setXY(oContainer,pos);
        return true;
    };
    
    //additional parameters:
	component.generateRequest = function(sQuery) {
        return "?cmd=dumpindex&results=100&query=" + sQuery ;
    };   

    return {
        oDS: dataSource,
        oAC: component
    };
    

//////End custom code

 		}//onSuccess
        
 	});//insert
})();



